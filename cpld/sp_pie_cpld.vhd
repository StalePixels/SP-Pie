-- SP-Pie CPLD logic for the TK-Pie board (Altera EPM240T100C5, MAX II).
--
-- The CPLD watches the Spectrum bus, turns screen writes and port writes into
-- events, queues them, and hands them to the Raspberry Pi through SMI reads.
-- The contract with the Pi is docs/protocol.md.
--
-- The design never drives the Spectrum bus. D0-D7 and /RESET are inputs and
-- /ROMCS is held at high impedance.
--
-- Clock domains
--
--   Write side: clocked by wclk = /WR and /RESET. Each Z80 write ends with a
--   wclk rising edge, and that edge stores the event and advances the write
--   pointer. The end of a reset (/RESET rising, with /WR high) is one more
--   wclk rising edge, and that edge stores the reset event. The
--   edge-connector clock (pin 62) is not used, so the logic does not depend on
--   its phase (inverted on the +2A/+3) or on clock stretching.
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
    -- Spectrum bus (all inputs).
    BUS_A           : in  std_logic_vector(15 downto 0);
    BUS_D           : in  std_logic_vector(7 downto 0);
    BUS_MRQ         : in  std_logic;  -- /MREQ
    BUS_IORQ_i      : in  std_logic;  -- /IORQ, pin 98
    BUS_IORQ_COPY_i : in  std_logic;  -- /IORQ, pin 76 (same net on the board)
    BUS_RD          : in  std_logic;  -- /RD, not used
    BUS_WR          : in  std_logic;  -- /WR
    BUS_RESET       : in  std_logic;  -- /RESET, input only
    BUS_ROMCS       : out std_logic;  -- /ROMCS, always high impedance
    M1              : in  std_logic;  -- /M1, not used
    CLK             : in  std_logic;  -- edge-connector clock, not used

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

  -- Stored TYPE code. The codes are the protocol TYPE values, 0 to 7.
  --
  -- One I/O write that matches both the 0xFE and the 0x1FFD decodes is stored
  -- as one entry with code 0 and OFFSET bit 0 set. A reset entry is code 0
  -- with OFFSET 0. I/O and reset events always send OFFSET 0, so OFFSET bit 0
  -- of a code 0 entry is free to tell the two apart, and no bit is added to
  -- each entry. The read side sends the double entry as two events, TYPE 3
  -- then TYPE 7, with the same DATA and OFFSET 0. Storing one entry keeps the
  -- write pointer to single steps, which the Gray-code crossing needs.
  constant T_RESET  : std_logic_vector(2 downto 0) := "000";
  constant T_BANK5  : std_logic_vector(2 downto 0) := "001";
  constant T_BANK7  : std_logic_vector(2 downto 0) := "010";
  constant T_FE     : std_logic_vector(2 downto 0) := "011";
  constant T_BF3B   : std_logic_vector(2 downto 0) := "100";
  constant T_FF3B   : std_logic_vector(2 downto 0) := "101";
  constant T_7FFD   : std_logic_vector(2 downto 0) := "110";
  constant T_1FFD   : std_logic_vector(2 downto 0) := "111";

  -- Queue storage. Each entry holds TYPE, OFFSET, DATA and the OVF mark.
  -- V is not stored: an entry is valid when it lies between the pointers.
  type type_arr_t is array (0 to DEPTH - 1) of std_logic_vector(2 downto 0);
  type off_arr_t  is array (0 to DEPTH - 1) of std_logic_vector(12 downto 0);
  type dat_arr_t  is array (0 to DEPTH - 1) of std_logic_vector(7 downto 0);
  signal q_type : type_arr_t;
  signal q_off  : off_arr_t;
  signal q_dat  : dat_arr_t;
  signal q_ovf  : std_logic_vector(DEPTH - 1 downto 0);

  ---------------------------------------------------------------------------
  -- Write side (/WR domain)
  ---------------------------------------------------------------------------
  signal wclk      : std_logic;
  signal iorq_n    : std_logic;
  signal io_set    : std_logic;
  signal io_seen   : std_logic := '0';
  signal mreq_seen : std_logic := '0';
  signal rst_seen  : std_logic := '0';

  -- Paging state: port 0x7FFD bits 5 (lock) and 2..0 (bank at 0xC000), and
  -- port 0x1FFD bits 2..0 (layout and special-paging enable).
  signal r7ffd_lock : std_logic := '0';
  signal r7ffd_bank : std_logic_vector(2 downto 0) := (others => '0');
  signal r1ffd      : std_logic_vector(2 downto 0) := (others => '0');

  signal ev_hit  : std_logic;
  signal ev_type : std_logic_vector(2 downto 0);
  signal ev_off  : std_logic_vector(12 downto 0);
  signal ev_dat  : std_logic_vector(7 downto 0);
  signal wr_7ffd : std_logic;
  signal wr_1ffd : std_logic;

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
  signal pend2     : std_logic := '0';  -- the current entry still owes its TYPE 7 event
  signal dbl       : std_logic := '0';  -- the pair on PI_SD comes from a double entry

begin

  BUS_ROMCS <= 'Z';
  PI_SD16   <= '0';
  PI_SD17   <= '0';

  -- Pins 98 and 76 both carry /IORQ. Either one low means /IORQ is low.
  iorq_n <= BUS_IORQ_i and BUS_IORQ_COPY_i;

  -- Write-side clock. While /RESET is high it is /WR. While /RESET is low it
  -- is low, so /WR edges during a reset are ignored. The Z80 holds /WR high
  -- while /RESET is low and after it, so the end of a reset gives one rising
  -- edge of its own and does not merge with the next write.
  wclk <= BUS_WR and BUS_RESET;

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
  -- Reset detection: rst_seen is set asynchronously while /RESET is low and
  -- cleared at the next wclk falling edge, which is the /WR falling edge of
  -- the first write after the reset. At the wclk rising edge made by /RESET
  -- going high, rst_seen is 1 and the decode queues a reset event. At the
  -- end of every later write it is 0. rst_seen does not change at that edge
  -- (it was set before and stays set), so the edge needs no synchronizer.
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

  p_rst_seen : process (wclk, BUS_RESET)
  begin
    if BUS_RESET = '0' then
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
  -- one taken when /RESET went low (or the last /WR falling edge before it).
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

  -- Event decode, from the address held at the wclk rising edge and the
  -- paging state left by earlier writes. At the end of a reset, the event is
  -- a reset event with DATA 0 and OFFSET 0, whatever the bus holds.
  p_decode : process (BUS_A, BUS_D, mreq_seen, io_seen, rst_seen,
                      r7ffd_lock, r7ffd_bank, r1ffd)
    variable a          : std_logic_vector(15 downto 0);
    variable in_screen  : boolean;
    variable bank5      : boolean;
    variable bank7      : boolean;
    variable layout     : std_logic_vector(1 downto 0);
    variable p_fe       : boolean;
    variable p_bf3b     : boolean;
    variable p_ff3b     : boolean;
    variable p_7ffd     : boolean;
    variable p_1ffd     : boolean;
  begin
    a      := BUS_A;
    layout := r1ffd(2 downto 1);

    -- OFFSET inside the 16 KB bank is A13..A0; events cover 0x0000-0x1AFF.
    in_screen := a(13) = '0' and unsigned(a(12 downto 8)) < "11011";

    if r1ffd(0) = '0' then
      -- Normal paging: bank 5 at 0x4000, the 0x7FFD bank at 0xC000.
      bank5 := a(15 downto 14) = "01" or
               (a(15 downto 14) = "11" and r7ffd_bank = "101");
      bank7 := a(15 downto 14) = "11" and r7ffd_bank = "111";
    else
      -- Special paging (+2A/+3). Bank 5 is at 0x4000 in layouts 01 and 10.
      -- Bank 7 is at 0x4000 in layout 11 and at 0xC000 in layout 01.
      bank5 := a(15 downto 14) = "01" and (layout = "01" or layout = "10");
      bank7 := (a(15 downto 14) = "01" and layout = "11") or
               (a(15 downto 14) = "11" and layout = "01");
    end if;

    p_fe   := a(0) = '0';
    p_bf3b := a(15 downto 14) = "10" and a(7 downto 6) = "00" and a(2) = '0' and a(0) = '1';
    p_ff3b := a(15 downto 14) = "11" and a(7 downto 6) = "00" and a(2) = '0' and a(0) = '1';
    p_7ffd := a(15 downto 14) = "01" and a(1 downto 0) = "01" and r7ffd_lock = '0';
    p_1ffd := a(15 downto 13) = "000" and a(12) = '1' and a(1) = '0' and r7ffd_lock = '0';

    ev_hit  <= '0';
    ev_type <= T_RESET;
    ev_off  <= (others => '0');
    ev_dat  <= BUS_D;
    wr_7ffd <= '0';
    wr_1ffd <= '0';

    if rst_seen = '1' then
      ev_hit <= '1';
      ev_dat <= (others => '0');
    elsif mreq_seen = '1' then
      if in_screen and bank5 then
        ev_hit  <= '1';
        ev_type <= T_BANK5;
        ev_off  <= a(12 downto 0);
      elsif in_screen and bank7 then
        ev_hit  <= '1';
        ev_type <= T_BANK7;
        ev_off  <= a(12 downto 0);
      end if;
    elsif io_seen = '1' then
      -- Of all the I/O decodes, only 0xFE and 0x1FFD can match together.
      if p_fe or p_bf3b or p_ff3b or p_7ffd or p_1ffd then
        ev_hit <= '1';
      end if;
      if p_fe and p_1ffd then
        -- Double entry: code 0 with OFFSET bit 0 set.
        ev_type   <= T_RESET;
        ev_off(0) <= '1';
      elsif p_fe then
        ev_type <= T_FE;
      elsif p_bf3b then
        ev_type <= T_BF3B;
      elsif p_ff3b then
        ev_type <= T_FF3B;
      elsif p_7ffd then
        ev_type <= T_7FFD;
      else
        ev_type <= T_1FFD;
      end if;
      if p_7ffd then
        wr_7ffd <= '1';
      end if;
      if p_1ffd then
        wr_1ffd <= '1';
      end if;
    end if;
  end process;

  -- Paging registers. Cleared while /RESET is low. A write is accepted only
  -- while 0x7FFD bit 5 is clear (the decode above already includes the lock).
  p_paging : process (wclk, BUS_RESET)
  begin
    if BUS_RESET = '0' then
      r7ffd_lock <= '0';
      r7ffd_bank <= (others => '0');
      r1ffd      <= (others => '0');
    elsif rising_edge(wclk) then
      if wr_7ffd = '1' then
        r7ffd_lock <= BUS_D(5);
        r7ffd_bank <= BUS_D(2 downto 0);
      end if;
      if wr_1ffd = '1' then
        r1ffd <= BUS_D(2 downto 0);
      end if;
    end if;
  end process;

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
      if ev_hit = '1' and q_full = '0' then
        wp_bin := gray2bin(wp_gray);
        idx    := to_integer(wp_bin(IW - 1 downto 0));
        q_type(idx) <= ev_type;
        q_off(idx)  <= ev_off;
        q_dat(idx)  <= ev_dat;
        q_ovf(idx)  <= ovf_flag;
      end if;
    end if;
  end process;

  p_wptr : process (wclk, PI_SYNC)
  begin
    if PI_SYNC = '1' then
      wp_gray  <= (others => '0');
      ovf_flag <= '0';
    elsif rising_edge(wclk) then
      if ev_hit = '1' then
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
  -- After half-word 0 is read, PI_SD is loaded with half-word 1 of the same
  -- entry. After half-word 1 is read, the entry is removed (if the pair was an
  -- event) and the next entry is loaded as half-word 0, or an empty pair when
  -- the queue is empty. A double entry (code 0, OFFSET bit 0 set) is sent as a
  -- TYPE 3 pair, then a TYPE 7 pair, both with OFFSET 0, and removed after the
  -- second. dbl is 1 for both pairs and clears OFFSET bit 0 in half-word 1.
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
    variable t      : std_logic_vector(2 downto 0);
  begin
    if PI_SYNC = '1' then
      rp_gray   <= (others => '0');
      wp_rsync1 <= (others => '0');
      wp_rsync2 <= (others => '0');
      sd_r      <= (others => '0');
      hw1       <= '0';
      cur_valid <= '0';
      pend2     <= '0';
      dbl       <= '0';
    elsif rising_edge(PI_SOE) then
      wp_rsync1 <= wp_gray;
      wp_rsync2 <= wp_rsync1;

      rp_bin := gray2bin(rp_gray);
      ridx   := to_integer(rp_bin(IW - 1 downto 0));

      if hw1 = '0' then
        -- Half-word 0 was read. Present half-word 1: H = 1, OFFSET.
        sd_r <= (others => '0');
        if cur_valid = '1' then
          sd_r(12 downto 1) <= q_off(ridx)(12 downto 1);
          sd_r(0)           <= q_off(ridx)(0) and not dbl;
        end if;
        hw1 <= '1';
      else
        -- Half-word 1 was read. Finish the pair and present the next half-word 0.
        if cur_valid = '1' and pend2 = '0' then
          rp_n := rp_bin + 1;
        else
          rp_n := rp_bin;
        end if;
        nidx := to_integer(rp_n(IW - 1 downto 0));
        t    := q_type(nidx);

        sd_r <= (others => '0');
        if pend2 = '1' then
          -- Second event of a double entry: TYPE 7, OVF = 0.
          sd_r(14)           <= '1';
          sd_r(12 downto 10) <= T_1FFD;
          sd_r(7 downto 0)   <= q_dat(nidx);
          cur_valid          <= '1';
          pend2              <= '0';
        elsif bin2gray(rp_n) /= wp_rsync2 then
          sd_r(14) <= '1';
          sd_r(13) <= q_ovf(nidx);
          if t = T_RESET and q_off(nidx)(0) = '1' then
            sd_r(12 downto 10) <= T_FE;
            pend2              <= '1';
            dbl                <= '1';
          else
            sd_r(12 downto 10) <= t;
            pend2              <= '0';
            dbl                <= '0';
          end if;
          sd_r(7 downto 0) <= q_dat(nidx);
          cur_valid        <= '1';
        else
          -- Queue empty: empty pair, half-word 0 is all zero.
          cur_valid <= '0';
          pend2     <= '0';
          dbl       <= '0';
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
