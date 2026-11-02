-- SP-Pie CPLD logic for the TK-Pie board (Altera EPM240T100C5, MAX II).
--
-- The CPLD watches the Spectrum bus and stores every Z80 memory write and
-- every I/O write as one event: the full address, the data byte and the
-- event kind. It does no address decode for events and keeps no paging
-- state. It queues the events and hands them to the Raspberry Pi through SMI
-- reads. The contract with the Pi is docs/protocol.md.
--
-- The design drives the Spectrum bus only to answer a read of the ULAplus
-- data port 0xFF3B: D0-D7 carry the answer while /IORQ and /RD are low with
-- that port's address, and are high impedance at all other times. /RESET is
-- an input and /ROMCS is held at high impedance.
--
-- Clock domains
--
--   Write side: clocked by wclk = /WR and rst_n, where rst_n is /RESET after
--   a filter. Each Z80 write ends with a wclk rising edge, and that edge
--   stores the event and advances the write pointer. The end of a reset
--   (rst_n rising, with /WR high) is one more wclk rising edge, and that edge
--   stores the reset event. The edge-connector clock (pin 62) clocks only the
--   /RESET filter, so the capture does not depend on its phase (inverted on
--   the +2A/+3) or on clock stretching.
--
--   Read side: clocked by the rising edge of the Pi SMI read strobe SOE
--   (GPIO 6). Each rising edge ends one SMI read and loads the next half-word.
--
--   The two sides share the queue storage and exchange the queue pointers in
--   Gray code. See the "Clock-domain crossing" notes below.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sp_pie_cpld is
  generic (
    -- Number of queue entries. Must be a power of two, at least 2.
    DEPTH : positive := 4
  );
  port (
    -- Spectrum bus (all inputs, except D0-D7 for ULAplus reads).
    BUS_A           : in  std_logic_vector(15 downto 0);
    BUS_D           : inout std_logic_vector(7 downto 0);  -- driven only for ULAplus reads
    BUS_MRQ         : in  std_logic;  -- /MREQ
    BUS_IORQ_i      : in  std_logic;  -- /IORQ, pin 98
    BUS_IORQ_COPY_i : in  std_logic;  -- /IORQ, pin 76 (same net on the board)
    BUS_RD          : in  std_logic;  -- /RD, for ULAplus reads
    BUS_WR          : in  std_logic;  -- /WR
    BUS_RESET       : in  std_logic;  -- /RESET, input only
    BUS_ROMCS       : out std_logic;  -- /ROMCS, always high impedance
    M1              : in  std_logic;  -- /M1, for the interrupt acknowledge
    CLK             : in  std_logic;  -- edge-connector clock, filters /RESET

    -- Raspberry Pi SMI and GPIO.
    PI_SA           : in  std_logic_vector(5 downto 0);  -- GPIO 0-5, ignored
    PI_SOE          : in  std_logic;                     -- GPIO 6, read strobe, active low
    PI_SWE          : in  std_logic;                     -- GPIO 7, ignored
    PI_SD           : out std_logic_vector(15 downto 0); -- GPIO 8-23, read data
    PI_SD16         : out std_logic;                     -- GPIO 24, driven low
    PI_SD17         : out std_logic;                     -- GPIO 25, driven low
    PI_SYNC         : in  std_logic;                     -- GPIO 26, high resets the queue
    PI_NOT_EMPTY    : out std_logic                      -- GPIO 27, high when queued
  );
end entity sp_pie_cpld;

architecture rtl of sp_pie_cpld is

  function log2(n : positive) return natural is
    variable r : natural := 0;
    variable v : positive := 1;
  begin
    while v < n loop
      v := v * 2;
      r := r + 1;
    end loop;
    return r;
  end function;

  -- Pointers carry one bit more than the slot index. The extra bit tells a
  -- full queue (pointers differ by DEPTH) from an empty one (pointers equal).
  constant IW : natural := log2(DEPTH);
  constant AW : natural := IW + 1;
  subtype ptr_t is unsigned(AW - 1 downto 0);

  function bin2gray(b : ptr_t) return ptr_t is
  begin
    return b xor shift_right(b, 1);
  end function;

  function gray2bin(g : ptr_t) return ptr_t is
    variable b : ptr_t;
  begin
    b(AW - 1) := g(AW - 1);
    for i in AW - 2 downto 0 loop
      b(i) := b(i + 1) xor g(i);
    end loop;
    return b;
  end function;

  -- Event KIND codes, as sent in half-word 0. Code 11 is never sent.
  constant K_MEM   : std_logic_vector(1 downto 0) := "00";
  constant K_IO    : std_logic_vector(1 downto 0) := "01";
  constant K_RESET : std_logic_vector(1 downto 0) := "10";

  -- Queue storage. Each entry holds the address, DATA, KIND, the OVF mark
  -- and the F mark. V is not stored: an entry is valid when it lies between
  -- the pointers.
  type adr_arr_t  is array (0 to DEPTH - 1) of std_logic_vector(15 downto 0);
  type dat_arr_t  is array (0 to DEPTH - 1) of std_logic_vector(7 downto 0);
  type kind_arr_t is array (0 to DEPTH - 1) of std_logic_vector(1 downto 0);
  signal q_adr  : adr_arr_t;
  signal q_dat  : dat_arr_t;
  signal q_kind : kind_arr_t;
  signal q_ovf  : std_logic_vector(DEPTH - 1 downto 0);
  signal q_frm  : std_logic_vector(DEPTH - 1 downto 0);

  ---------------------------------------------------------------------------
  -- Write side (/WR domain)
  ---------------------------------------------------------------------------
  signal wclk      : std_logic;
  signal rst_sr    : std_logic_vector(3 downto 0) := (others => '0');
  signal rst_n     : std_logic := '0';  -- /RESET after the filter
  signal iorq_n    : std_logic;
  signal io_set    : std_logic;
  signal io_seen   : std_logic := '0';
  signal mreq_seen : std_logic := '0';
  signal rst_seen  : std_logic := '0';
  signal inta      : std_logic;
  signal frm_flag  : std_logic := '0';

  signal st_hit  : std_logic;  -- the wclk rising edge stores an event
  signal st_kind : std_logic_vector(1 downto 0);
  signal st_adr  : std_logic_vector(15 downto 0);
  signal st_dat  : std_logic_vector(7 downto 0);
  signal wr_bf3b : std_logic;
  signal wr_ff3b : std_logic;

  -- ULAplus read-back: the register port group (1 = mode group), the mode
  -- register, and the last byte written to the data port.
  signal ulap_modegrp : std_logic := '0';
  signal ulap_mode    : std_logic_vector(7 downto 0) := (others => '0');
  signal ulap_last    : std_logic_vector(7 downto 0) := (others => '0');
  signal rd_ff3b      : std_logic;

  signal wp_gray  : ptr_t := (others => '0');
  signal rp_wsync : ptr_t := (others => '0');  -- read pointer seen by the write side
  signal q_full   : std_logic;
  signal ovf_flag : std_logic := '0';

  ---------------------------------------------------------------------------
  -- Read side (SOE domain)
  ---------------------------------------------------------------------------
  signal rp_gray   : ptr_t := (others => '0');
  signal wp_rsync1 : ptr_t := (others => '0');
  signal wp_rsync2 : ptr_t := (others => '0');  -- write pointer seen by the read side
  signal sd_r      : std_logic_vector(14 downto 0) := (others => '0');
  signal hw1       : std_logic := '0';  -- 1 while PI_SD holds half-word 1
  signal cur_valid : std_logic := '0';  -- the pair on PI_SD is an event, not empty

begin

  BUS_ROMCS <= 'Z';
  PI_SD16   <= '0';
  PI_SD17   <= '0';

  -- Pins 98 and 76 both carry /IORQ. Either one low means /IORQ is low.
  iorq_n <= BUS_IORQ_i and BUS_IORQ_COPY_i;

  -- /RESET filter. The edge-connector /RESET line can carry pulses of a few
  -- nanoseconds that are not resets. A real reset lasts milliseconds. /RESET
  -- is sampled on each rising edge of the edge-connector clock, and rst_n
  -- changes only when the last four samples agree (about 1.1 us at 3.5 MHz).
  -- rst_n is a register output, so it has clean edges. The clock runs during
  -- a reset. Its phase does not matter here.
  p_rst_filter : process (CLK)
  begin
    if rising_edge(CLK) then
      rst_sr <= rst_sr(2 downto 0) & BUS_RESET;
      if rst_sr = "0000" then
        rst_n <= '0';
      elsif rst_sr = "1111" then
        rst_n <= '1';
      end if;
    end if;
  end process;

  -- Write-side clock. While rst_n is high it is /WR. While rst_n is low it
  -- is low, so /WR edges during a reset are ignored. The Z80 holds /WR high
  -- while /RESET is low and for some microseconds after it, so the end of a
  -- reset gives one rising edge of its own and does not merge with the next
  -- write.
  wclk <= BUS_WR and rst_n;

  ---------------------------------------------------------------------------
  -- Write capture
  --
  -- The event is stored on the wclk rising edge at the end of the write. At that
  -- edge D0-D7 are valid for memory writes (valid before /WR falls) and for
  -- I/O writes (valid shortly after /WR falls), and the Z80 holds address and
  -- data for about half a T-state after /WR rises. Capture at the falling
  -- edge would miss late I/O data, so it is not used.
  --
  -- /MREQ and /IORQ rise at the same clock edge as /WR, so they cannot be read
  -- at the /WR rising edge. They are remembered while /WR is low:
  --
  --   mreq_seen: /MREQ sampled at the wclk falling edge. On a memory write
  --   /MREQ is low one T-state before /WR falls.
  --
  --   io_seen: on an I/O write /IORQ and /WR fall at the same clock edge, in no
  --   fixed order, so a falling-edge sample of /IORQ could miss it. io_seen is
  --   cleared at the wclk falling edge and set asynchronously while /IORQ and
  --   /WR are both low (about 2.5 T-states on an I/O write). It holds its value
  --   through the wclk rising edge.
  --
  -- Reset detection: rst_seen is set asynchronously while rst_n is low and
  -- cleared at the next wclk falling edge, which is the /WR falling edge of
  -- the first write after the reset. At the wclk rising edge made by rst_n
  -- going high, rst_seen is 1 and a reset event is stored. At the end of
  -- every later write it is 0. rst_seen does not change at that edge (it was
  -- set before and stays set), so the edge needs no synchronizer.
  ---------------------------------------------------------------------------
  io_set <= (not iorq_n) and (not BUS_WR);

  p_io_seen : process (wclk, io_set)
  begin
    if io_set = '1' then
      io_seen <= '1';
    elsif falling_edge(wclk) then
      io_seen <= '0';
    end if;
  end process;

  -- Frame flag. The Spectrum interrupts the Z80 once per frame, and the Z80
  -- acknowledges with /M1 and /IORQ low together, a combination no other
  -- cycle has. frm_flag is set asynchronously during the acknowledge and
  -- cleared at the wclk rising edge that stores an event, so the first event
  -- stored after an acknowledge carries F = 1. An event dropped on a full
  -- queue leaves the flag set. The acknowledge never overlaps a write.
  inta <= (not M1) and (not iorq_n);

  p_frm : process (wclk, inta)
  begin
    if inta = '1' then
      frm_flag <= '1';
    elsif rising_edge(wclk) then
      if st_hit = '1' and q_full = '0' then
        frm_flag <= '0';
      end if;
    end if;
  end process;

  p_rst_seen : process (wclk, rst_n)
  begin
    if rst_n = '0' then
      rst_seen <= '1';
    elsif falling_edge(wclk) then
      rst_seen <= '0';
    end if;
  end process;

  -- Clock-domain crossing, read pointer into the write side: rp_gray is
  -- sampled at the wclk falling edge and first used at the next wclk rising
  -- edge. Gray code changes one bit per step, so the sample is either the old
  -- or the new pointer, never a mix. The sample has the whole wclk low time
  -- (at least one T-state, about 280 ns at 3.5 MHz) to settle from
  -- metastability before it is used. For the reset event, the sample is the
  -- one taken when rst_n went low (or the last /WR falling edge before it).
  -- A stale read pointer only makes the queue look fuller than it is; it can
  -- never let a write overrun an entry the Pi has not read.
  p_rp_wsync : process (wclk, PI_SYNC)
  begin
    if PI_SYNC = '1' then
      rp_wsync <= (others => '0');
    elsif falling_edge(wclk) then
      rp_wsync <= rp_gray;
    end if;
  end process;

  p_mreq_seen : process (wclk)
  begin
    if falling_edge(wclk) then
      mreq_seen <= not BUS_MRQ;
    end if;
  end process;

  -- The event for this wclk rising edge. Every memory write and every I/O
  -- write is an event, with the address and data held on the bus. At the end
  -- of a reset the event is a reset event with address 0 and DATA 0,
  -- whatever the bus holds.
  st_hit <= rst_seen or mreq_seen or io_seen;

  st_kind <= K_RESET when rst_seen = '1' else
             K_MEM   when mreq_seen = '1' else
             K_IO;

  st_adr <= (others => '0') when rst_seen = '1' else BUS_A;
  st_dat <= (others => '0') when rst_seen = '1' else BUS_D;

  -- ULAplus port decodes for the read-back registers. 0xBF3B: A15..A14 = 10,
  -- A7..A6 = 00, A2 = 0, A0 = 1. 0xFF3B: the same with A15..A14 = 11.
  wr_bf3b <= '1' when io_seen = '1' and rst_seen = '0' and mreq_seen = '0' and
                      BUS_A(15 downto 14) = "10" and BUS_A(7 downto 6) = "00" and
                      BUS_A(2) = '0' and BUS_A(0) = '1'
             else '0';
  wr_ff3b <= '1' when io_seen = '1' and rst_seen = '0' and mreq_seen = '0' and
                      BUS_A(15 downto 14) = "11" and BUS_A(7 downto 6) = "00" and
                      BUS_A(2) = '0' and BUS_A(0) = '1'
             else '0';

  -- ULAplus registers for read-back. Cleared while /RESET is low.
  p_ulaplus : process (wclk, rst_n)
  begin
    if rst_n = '0' then
      ulap_modegrp <= '0';
      ulap_mode    <= (others => '0');
      ulap_last    <= (others => '0');
    elsif rising_edge(wclk) then
      if wr_bf3b = '1' then
        if BUS_D(7 downto 6) = "01" then
          ulap_modegrp <= '1';
        else
          ulap_modegrp <= '0';
        end if;
      end if;
      if wr_ff3b = '1' then
        ulap_last <= BUS_D;
        if ulap_modegrp = '1' then
          ulap_mode <= BUS_D;
        end if;
      end if;
    end if;
  end process;

  -- ULAplus read of port 0xFF3B (same decode as the write). With the mode
  -- group selected the answer is the mode register, otherwise the last byte
  -- written to the data port. The CPLD keeps no copy of the palette.
  rd_ff3b <= '1' when iorq_n = '0' and BUS_RD = '0' and
                      BUS_A(15 downto 14) = "11" and BUS_A(7 downto 6) = "00" and
                      BUS_A(2) = '0' and BUS_A(0) = '1'
             else '0';

  BUS_D <= ulap_mode when rd_ff3b = '1' and ulap_modegrp = '1' else
           ulap_last when rd_ff3b = '1' else
           (others => 'Z');

  -- Full when the write pointer is DEPTH entries ahead of the read pointer.
  p_full : process (wp_gray, rp_wsync)
    variable used : ptr_t;
  begin
    used := gray2bin(wp_gray) - gray2bin(rp_wsync);
    q_full <= used(AW - 1);
  end process;

  -- Queue write. An event that finds the queue full is dropped and sets the
  -- overflow flag. The next stored event carries OVF = 1 and clears the flag,
  -- so OVF marks the first event after the gap. A reset event follows the
  -- same rule: when the queue is full it is dropped, and the first event
  -- stored after it carries OVF = 1.
  p_store : process (wclk)
    variable wp_bin : ptr_t;
    variable idx    : natural range 0 to DEPTH - 1;
  begin
    if rising_edge(wclk) then
      if st_hit = '1' and q_full = '0' then
        wp_bin := gray2bin(wp_gray);
        idx    := to_integer(wp_bin(IW - 1 downto 0));
        q_adr(idx)  <= st_adr;
        q_dat(idx)  <= st_dat;
        q_kind(idx) <= st_kind;
        q_ovf(idx)  <= ovf_flag;
        q_frm(idx)  <= frm_flag;
      end if;
    end if;
  end process;

  p_wptr : process (wclk, PI_SYNC)
  begin
    if PI_SYNC = '1' then
      wp_gray  <= (others => '0');
      ovf_flag <= '0';
    elsif rising_edge(wclk) then
      if st_hit = '1' then
        if q_full = '0' then
          wp_gray  <= bin2gray(gray2bin(wp_gray) + 1);
          ovf_flag <= '0';
        else
          ovf_flag <= '1';
        end if;
      end if;
    end if;
  end process;

  ---------------------------------------------------------------------------
  -- SMI read side
  --
  -- PI_SD changes only on the SOE rising edge, the end of a read, so each read
  -- sees data that has been stable since the previous read ended.
  --
  -- Half-word 0: H = 0, V, OVF, F, KIND, 0, A15, DATA.
  -- Half-word 1: H = 1, A14..A0.
  --
  -- After half-word 0 is read, PI_SD is loaded with half-word 1 of the same
  -- entry. After half-word 1 is read, the entry is removed (if the pair was an
  -- event) and the next entry is loaded as half-word 0, or an empty pair when
  -- the queue is empty.
  --
  -- Clock-domain crossing, write pointer into the read side: wp_gray passes
  -- through two registers clocked by SOE. Gray code makes each sample the old
  -- or the new pointer, and the second register gives the first a full SOE
  -- period to settle. A stale write pointer only hides new entries for a few
  -- reads (the Pi gets empty pairs); it can never expose an entry before its
  -- storage is written. An entry is written at the same wclk edge that advances
  -- wp_gray, and it is read at least two SOE edges after that.
  ---------------------------------------------------------------------------
  p_read : process (PI_SOE, PI_SYNC)
    variable rp_bin : ptr_t;
    variable rp_n   : ptr_t;
    variable ridx   : natural range 0 to DEPTH - 1;
    variable nidx   : natural range 0 to DEPTH - 1;
  begin
    if PI_SYNC = '1' then
      rp_gray   <= (others => '0');
      wp_rsync1 <= (others => '0');
      wp_rsync2 <= (others => '0');
      sd_r      <= (others => '0');
      hw1       <= '0';
      cur_valid <= '0';
    elsif rising_edge(PI_SOE) then
      wp_rsync1 <= wp_gray;
      wp_rsync2 <= wp_rsync1;

      rp_bin := gray2bin(rp_gray);
      ridx   := to_integer(rp_bin(IW - 1 downto 0));

      if hw1 = '0' then
        -- Half-word 0 was read. Present half-word 1: H = 1, A14..A0.
        sd_r <= (others => '0');
        if cur_valid = '1' then
          sd_r <= q_adr(ridx)(14 downto 0);
        end if;
        hw1 <= '1';
      else
        -- Half-word 1 was read. Finish the pair and present the next half-word 0.
        if cur_valid = '1' then
          rp_n := rp_bin + 1;
        else
          rp_n := rp_bin;
        end if;
        nidx := to_integer(rp_n(IW - 1 downto 0));

        sd_r <= (others => '0');
        if bin2gray(rp_n) /= wp_rsync2 then
          sd_r(14)           <= '1';
          sd_r(13)           <= q_ovf(nidx);
          sd_r(12)           <= q_frm(nidx);
          sd_r(11 downto 10) <= q_kind(nidx);
          sd_r(8)            <= q_adr(nidx)(15);
          sd_r(7 downto 0)   <= q_dat(nidx);
          cur_valid          <= '1';
        else
          -- Queue empty: empty pair, half-word 0 is all zero.
          cur_valid <= '0';
        end if;
        rp_gray <= bin2gray(rp_n);
        hw1     <= '0';
      end if;
    end if;
  end process;

  PI_SD <= hw1 & sd_r;

  -- NOT_EMPTY compares the two pointers directly, across the clock domains.
  -- It can glitch for a moment when a pointer changes. The protocol marks it
  -- as information only.
  PI_NOT_EMPTY <= '1' when wp_gray /= rp_gray else '0';

end architecture rtl;
