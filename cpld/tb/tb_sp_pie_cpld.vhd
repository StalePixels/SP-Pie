-- Self-checking testbench for sp_pie_cpld (VHDL-2008).
--
-- A Z80 bus model makes memory and I/O writes with Zilog cycle timing. A Pi
-- model reads the queue through SMI with a jittered SOE strobe that has no
-- fixed relation to the bus clock, checks the H bit of every half-word, and
-- records each event it receives. The main process compares the received
-- events with the expected list after each test step.
--
-- The run ends with "TB PASS" or with "TB FAIL" at severity failure, which
-- makes the simulator exit nonzero.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_sp_pie_cpld is
  generic (
    DEPTH : positive := 4
  );
end entity tb_sp_pie_cpld;

architecture sim of tb_sp_pie_cpld is

  -- Z80 clock at 3.5 MHz.
  constant TCPU      : time := 285714 ps;
  -- Output delays from a Z80 clock edge.
  constant T_ADDR    : time := 50 ns;
  constant T_CTRL    : time := 40 ns;
  -- Data hold after /WR rises (Z84C00 table minimum at 4 MHz). After this the
  -- model puts 'X' on the data bus, so late capture fails the checks.
  constant T_HOLD    : time := 60 ns;
  -- I/O write data arrives this long after /WR falls. Before that the data
  -- bus carries the inverted byte, so early capture fails the checks.
  constant T_IO_LATE : time := 30 ns;

  ---------------------------------------------------------------------------
  -- Event list shared by the processes.
  ---------------------------------------------------------------------------
  type ev_list_t is protected
    procedure push(v : std_logic_vector(31 downto 0));
    impure function pop return std_logic_vector;
    impure function count return natural;
    procedure clear;
  end protected;

  type ev_list_t is protected body
    type arr_t is array (0 to 1023) of std_logic_vector(31 downto 0);
    variable arr  : arr_t;
    variable head : natural := 0;
    variable tail : natural := 0;
    procedure push(v : std_logic_vector(31 downto 0)) is
    begin
      arr(tail) := v;
      tail      := tail + 1;
    end procedure;
    impure function pop return std_logic_vector is
      variable v : std_logic_vector(31 downto 0);
    begin
      v    := arr(head);
      head := head + 1;
      return v;
    end function;
    impure function count return natural is
    begin
      return tail - head;
    end function;
    procedure clear is
    begin
      head := 0;
      tail := 0;
    end procedure;
  end protected body;

  shared variable got : ev_list_t;
  shared variable exp : ev_list_t;

  -- The 32-bit word the Pi sees for one pair: half-word 1 in bits 31..16,
  -- half-word 0 in bits 15..0.
  function mk_ev(t : natural; ovf : std_logic; off : natural; dat : natural)
    return std_logic_vector is
    variable hw0, hw1 : std_logic_vector(15 downto 0);
  begin
    hw0 := '0' & '1' & ovf & std_logic_vector(to_unsigned(t, 3)) & "00" &
           std_logic_vector(to_unsigned(dat, 8));
    hw1 := '1' & "00" & std_logic_vector(to_unsigned(off, 13));
    return hw1 & hw0;
  end function;

  ---------------------------------------------------------------------------
  -- DUT connections
  ---------------------------------------------------------------------------
  signal cpu_clk  : std_logic := '0';
  signal clk_inv  : std_logic := '0';
  signal edge_clk : std_logic;

  signal a        : std_logic_vector(15 downto 0) := (others => '1');
  signal d        : std_logic_vector(7 downto 0)  := (others => 'X');
  signal mreq_n   : std_logic := '1';
  signal iorq_n   : std_logic := '1';
  signal rd_n     : std_logic := '1';
  signal wr_n     : std_logic := '1';
  signal reset_n  : std_logic := '0';
  signal m1_n     : std_logic := '1';
  signal romcs    : std_logic;

  signal sa       : std_logic_vector(5 downto 0) := (others => '0');
  signal soe      : std_logic;
  signal soe_r    : std_logic := '1';  -- driven by the Pi reader process
  signal soe_m    : std_logic := '1';  -- driven by the main process
  signal swe      : std_logic := '1';
  signal sd       : std_logic_vector(15 downto 0);
  signal sd16     : std_logic;
  signal sd17     : std_logic;
  signal sync     : std_logic := '0';
  signal nempty   : std_logic;

  -- Pi reader control and results.
  signal rd_en      : std_logic := '0';
  signal rd_idle    : std_logic := '1';
  signal rd_errors  : natural := 0;
  signal rd_empties : natural := 0;
  signal rd_pairs   : natural := 0;

begin

  cpu_clk  <= not cpu_clk after TCPU / 2;
  edge_clk <= cpu_clk xor clk_inv;
  soe      <= soe_r and soe_m;

  dut : entity work.sp_pie_cpld
    generic map (DEPTH => DEPTH)
    port map (
      BUS_A           => a,
      BUS_D           => d,
      BUS_MRQ         => mreq_n,
      BUS_IORQ_i      => iorq_n,
      BUS_IORQ_COPY_i => '1',
      BUS_RD          => rd_n,
      BUS_WR          => wr_n,
      BUS_RESET       => reset_n,
      BUS_ROMCS       => romcs,
      M1              => m1_n,
      CLK             => edge_clk,
      PI_SA           => sa,
      PI_SOE          => soe,
      PI_SWE          => swe,
      PI_SD           => sd,
      PI_SD16         => sd16,
      PI_SD17         => sd17,
      PI_SYNC         => sync,
      PI_NOT_EMPTY    => nempty
    );

  ---------------------------------------------------------------------------
  -- Pi reader. While rd_en is high it reads pairs back to back. SOE low and
  -- high times vary with a pseudo-random sequence. SD is sampled at the end
  -- of each strobe, just before SOE rises.
  ---------------------------------------------------------------------------
  p_reader : process
    variable lfsr   : unsigned(15 downto 0) := x"ACE1";
    variable hw0    : std_logic_vector(15 downto 0);
    variable hw1    : std_logic_vector(15 downto 0);
    variable errs   : natural := 0;
    variable emp    : natural := 0;
    variable pairs  : natural := 0;

    procedure step_lfsr is
    begin
      lfsr := lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
    end procedure;

    procedure smi_read(v : out std_logic_vector(15 downto 0)) is
    begin
      step_lfsr;
      soe_r <= '0';
      wait for 37 ns + (to_integer(lfsr(4 downto 0)) * 1 ns);
      v := sd;
      soe_r <= '1';
      step_lfsr;
      wait for 29 ns + (to_integer(lfsr(5 downto 0)) * 1 ns);
    end procedure;
  begin
    loop
      if rd_en = '0' then
        rd_idle <= '1';
        wait until rd_en = '1';
      end if;
      rd_idle <= '0';
      smi_read(hw0);
      smi_read(hw1);
      pairs := pairs + 1;
      if hw0(15) /= '0' or hw1(15) /= '1' then
        report "reader: H bit wrong, hw0=" & to_hstring(hw0) & " hw1=" & to_hstring(hw1)
          severity error;
        errs := errs + 1;
      elsif hw0(14) = '1' then
        got.push(hw1 & hw0);
      elsif hw0 /= x"0000" or hw1 /= x"8000" then
        report "reader: empty pair not clean, hw0=" & to_hstring(hw0) & " hw1=" & to_hstring(hw1)
          severity error;
        errs := errs + 1;
      else
        emp := emp + 1;
      end if;
      rd_errors  <= errs;
      rd_empties <= emp;
      rd_pairs   <= pairs;
    end loop;
  end process;

  ---------------------------------------------------------------------------
  -- Main test sequence.
  ---------------------------------------------------------------------------
  p_main : process
    variable errors : natural := 0;
    variable e0, e1 : natural;

    procedure fail(msg : string) is
    begin
      report "FAIL: " & msg severity error;
      errors := errors + 1;
    end procedure;

    -- Memory write cycle (Zilog UM0080 figure 6): address in T1, /MREQ and
    -- data from the middle of T1, /WR low from the falling edge of T2 to the
    -- falling edge of T3.
    procedure mem_write(addr : natural; dat : natural) is
    begin
      wait until rising_edge(cpu_clk);                      -- T1
      a <= transport std_logic_vector(to_unsigned(addr, 16)) after T_ADDR;
      wait until falling_edge(cpu_clk);
      mreq_n <= transport '0' after T_CTRL;
      d      <= transport std_logic_vector(to_unsigned(dat, 8)) after 20 ns;
      wait until rising_edge(cpu_clk);                      -- T2
      wait until falling_edge(cpu_clk);
      wr_n <= transport '0' after T_CTRL;
      wait until rising_edge(cpu_clk);                      -- T3
      wait until falling_edge(cpu_clk);
      wr_n   <= transport '1' after T_CTRL;
      mreq_n <= transport '1' after T_CTRL + 5 ns;
      d      <= transport (others => 'X') after T_CTRL + T_HOLD;
      a      <= transport (others => 'X') after TCPU / 2 + T_ADDR;
    end procedure;

    -- I/O write cycle (Zilog UM0080 figure 7): /IORQ and /WR fall after the
    -- rising edge of T2, one automatic wait state, both rise at the falling
    -- edge of T3. iorq_skew moves /IORQ against /WR (positive: /IORQ later).
    -- The data bus carries the inverted byte until T_IO_LATE after /WR falls.
    procedure io_write(addr : natural; dat : natural; iorq_skew : time := 0 ns) is
      variable v : std_logic_vector(7 downto 0);
    begin
      v := std_logic_vector(to_unsigned(dat, 8));
      wait until rising_edge(cpu_clk);                      -- T1
      a <= transport std_logic_vector(to_unsigned(addr, 16)) after T_ADDR;
      d <= transport not v after T_ADDR;
      wait until rising_edge(cpu_clk);                      -- T2
      wr_n   <= transport '0' after T_CTRL;
      iorq_n <= transport '0' after T_CTRL + iorq_skew;
      d      <= transport v after T_CTRL + T_IO_LATE;
      wait until rising_edge(cpu_clk);                      -- TW
      wait until rising_edge(cpu_clk);                      -- T3
      wait until falling_edge(cpu_clk);
      wr_n   <= transport '1' after T_CTRL;
      iorq_n <= transport '1' after T_CTRL;
      d      <= transport (others => 'X') after T_CTRL + T_HOLD;
      a      <= transport (others => 'X') after TCPU / 2 + T_ADDR;
    end procedure;

    procedure expect(t : natural; off : natural; dat : natural; ovf : std_logic := '0') is
    begin
      exp.push(mk_ev(t, ovf, off, dat));
    end procedure;

    procedure reader_on is
    begin
      rd_en <= '1';
      wait for 1 ns;
    end procedure;

    procedure reader_off is
    begin
      rd_en <= '0';
      wait for 1 ns;
      if rd_idle = '0' then
        wait until rd_idle = '1';
      end if;
    end procedure;

    -- Let the reader drain the queue, then compare received with expected.
    procedure check(name : string) is
      variable g, x : std_logic_vector(31 downto 0);
      variable ok   : boolean := true;
    begin
      wait for 2 us;
      if nempty = '1' then
        wait until nempty = '0' for 100 us;
      end if;
      wait for 2 us;
      if nempty /= '0' then
        fail(name & ": NOT_EMPTY still high after drain");
        ok := false;
      end if;
      if got.count /= exp.count then
        fail(name & ": got " & integer'image(got.count) & " events, expected " &
             integer'image(exp.count));
        ok := false;
      end if;
      while got.count > 0 and exp.count > 0 loop
        g := got.pop;
        x := exp.pop;
        if g /= x then
          fail(name & ": got " & to_hstring(g) & ", expected " & to_hstring(x));
          ok := false;
        end if;
      end loop;
      while got.count > 0 loop
        fail(name & ": extra event " & to_hstring(got.pop));
        ok := false;
      end loop;
      while exp.count > 0 loop
        fail(name & ": missing event " & to_hstring(exp.pop));
        ok := false;
      end loop;
      got.clear;
      exp.clear;
      if ok then
        report "ok   " & name;
      end if;
    end procedure;

    procedure pulse_sync is
    begin
      sync <= '1';
      wait for 1 us;
      sync <= '0';
      wait for 200 ns;
    end procedure;

    procedure pulse_reset is
    begin
      wait until rising_edge(cpu_clk);
      reset_n <= '0';
      wait for 3 * TCPU;
      reset_n <= '1';
      wait for TCPU;
    end procedure;

    -- One SMI read driven by the main process, for the desync test.
    procedure main_read(v : out std_logic_vector(15 downto 0)) is
    begin
      soe_m <= '0';
      wait for 50 ns;
      v := sd;
      soe_m <= '1';
      wait for 50 ns;
    end procedure;

    variable hw : std_logic_vector(15 downto 0);

  begin
    report "testbench DEPTH = " & integer'image(DEPTH);

    -- Power-up with /RESET low.
    wait for 1 us;
    reset_n <= '1';
    wait for 1 us;

    -- Static outputs.
    if romcs /= 'Z' then fail("/ROMCS is not high impedance"); end if;
    if sd16 /= '0' or sd17 /= '0' then fail("SD16/SD17 not low"); end if;

    -------------------------------------------------------------------------
    -- Empty queue: only clean empty pairs.
    -------------------------------------------------------------------------
    reader_on;
    e0 := rd_empties;
    wait for 3 us;
    e1 := rd_empties;
    if e1 - e0 < 10 then
      fail("empty queue: expected a stream of empty pairs, got " & integer'image(e1 - e0));
    end if;
    if nempty /= '0' then fail("empty queue: NOT_EMPTY high"); end if;
    check("empty queue gives empty pairs (0x0000, 0x8000)");

    -------------------------------------------------------------------------
    -- Bank 5 at 0x4000, normal paging.
    -------------------------------------------------------------------------
    mem_write(16#4000#, 16#A5#);  expect(1, 16#0000#, 16#A5#);
    mem_write(16#5AFF#, 16#3C#);  expect(1, 16#1AFF#, 16#3C#);
    mem_write(16#5800#, 16#47#);  expect(1, 16#1800#, 16#47#);
    mem_write(16#5B00#, 16#11#);  -- OFFSET 0x1B00: no event
    mem_write(16#5FFF#, 16#12#);  -- no event
    mem_write(16#6000#, 16#13#);  -- OFFSET 0x2000: no event
    mem_write(16#7FFF#, 16#14#);  -- no event
    mem_write(16#3FFF#, 16#15#);  -- ROM area: no event
    mem_write(16#8000#, 16#16#);  -- bank 2: no event
    mem_write(16#C000#, 16#17#);  -- bank 0 at 0xC000: no event
    check("bank 5 at 0x4000, inside and outside OFFSET < 0x1B00");

    -------------------------------------------------------------------------
    -- 0x7FFD paging: bank 7 and bank 5 at 0xC000.
    -------------------------------------------------------------------------
    io_write(16#7FFD#, 16#07#);   expect(6, 0, 16#07#);
    mem_write(16#C123#, 16#21#);  expect(2, 16#0123#, 16#21#);
    mem_write(16#DAFF#, 16#22#);  expect(2, 16#1AFF#, 16#22#);
    mem_write(16#DB00#, 16#23#);  -- no event
    mem_write(16#4001#, 16#24#);  expect(1, 16#0001#, 16#24#);
    io_write(16#7FFD#, 16#0D#);   expect(6, 0, 16#0D#);  -- bank 5, screen bit set
    mem_write(16#C010#, 16#25#);  expect(1, 16#0010#, 16#25#);
    io_write(16#7FFD#, 16#03#);   expect(6, 0, 16#03#);
    mem_write(16#C010#, 16#26#);  -- bank 3: no event
    io_write(16#7FFD#, 16#00#);   expect(6, 0, 16#00#);
    check("0x7FFD paging, bank 7 and bank 5 at 0xC000");

    -------------------------------------------------------------------------
    -- +3 special paging via 0x1FFD.
    -------------------------------------------------------------------------
    io_write(16#7FFD#, 16#07#);   expect(6, 0, 16#07#);  -- must not matter in special mode
    io_write(16#1FFD#, 16#01#);   expect(7, 0, 16#01#);  -- layout 00: 0 1 2 3
    mem_write(16#0000#, 16#31#);
    mem_write(16#4000#, 16#32#);
    mem_write(16#8000#, 16#33#);
    mem_write(16#C000#, 16#34#);
    io_write(16#1FFD#, 16#03#);   expect(7, 0, 16#03#);  -- layout 01: 4 5 6 7
    mem_write(16#0000#, 16#35#);
    mem_write(16#4100#, 16#36#);  expect(1, 16#0100#, 16#36#);
    mem_write(16#8000#, 16#37#);
    mem_write(16#C200#, 16#38#);  expect(2, 16#0200#, 16#38#);
    io_write(16#1FFD#, 16#05#);   expect(7, 0, 16#05#);  -- layout 10: 4 5 6 3
    mem_write(16#0000#, 16#39#);
    mem_write(16#4300#, 16#3A#);  expect(1, 16#0300#, 16#3A#);
    mem_write(16#8000#, 16#3B#);
    mem_write(16#C000#, 16#3C#);
    io_write(16#1FFD#, 16#07#);   expect(7, 0, 16#07#);  -- layout 11: 4 7 6 3
    mem_write(16#0000#, 16#3D#);
    mem_write(16#4400#, 16#3E#);  expect(2, 16#0400#, 16#3E#);
    mem_write(16#8000#, 16#3F#);
    mem_write(16#C000#, 16#40#);
    io_write(16#1FFD#, 16#00#);   expect(7, 0, 16#00#);  -- back to normal paging
    mem_write(16#C001#, 16#41#);  expect(2, 16#0001#, 16#41#);  -- 0x7FFD bank 7 again
    io_write(16#7FFD#, 16#00#);   expect(6, 0, 16#00#);
    check("+3 special paging, all four layouts");

    -------------------------------------------------------------------------
    -- Lock bit.
    -------------------------------------------------------------------------
    io_write(16#7FFD#, 16#27#);   expect(6, 0, 16#27#);  -- accepted, sets the lock
    io_write(16#7FFD#, 16#00#);                          -- locked: no event
    io_write(16#1FFD#, 16#03#);                          -- locked: no event
    mem_write(16#C005#, 16#51#);  expect(2, 16#0005#, 16#51#);  -- still bank 7
    mem_write(16#4005#, 16#52#);  expect(1, 16#0005#, 16#52#);  -- still normal paging
    io_write(16#00FE#, 16#05#);   expect(3, 0, 16#05#);  -- other ports still work
    io_write(16#1FFC#, 16#06#);   expect(3, 0, 16#06#);  -- 0xFE matches, 0x1FFD locked
    check("lock bit: locked writes ignored, no event");

    -------------------------------------------------------------------------
    -- /RESET clears paging and the lock.
    -------------------------------------------------------------------------
    pulse_reset;
    mem_write(16#C005#, 16#61#);                         -- bank 0: no event
    io_write(16#7FFD#, 16#07#);   expect(6, 0, 16#07#);  -- unlocked again
    mem_write(16#C006#, 16#62#);  expect(2, 16#0006#, 16#62#);
    io_write(16#1FFD#, 16#07#);   expect(7, 0, 16#07#);
    pulse_reset;
    mem_write(16#4007#, 16#63#);  expect(1, 16#0007#, 16#63#);  -- normal paging again
    mem_write(16#C007#, 16#64#);                         -- bank 0: no event
    check("/RESET clears paging and lock");

    -------------------------------------------------------------------------
    -- Every I/O event TYPE, late I/O data, /IORQ skew both ways.
    -------------------------------------------------------------------------
    io_write(16#00FE#, 16#02#);           expect(3, 0, 16#02#);
    io_write(16#BF3B#, 16#40#);           expect(4, 0, 16#40#);
    io_write(16#FF3B#, 16#01#, 10 ns);    expect(5, 0, 16#01#);
    io_write(16#7FFD#, 16#10#, -10 ns);   expect(6, 0, 16#10#);
    io_write(16#1FFD#, 16#04#, 15 ns);    expect(7, 0, 16#04#);
    io_write(16#FFFE#, 16#AA#, -15 ns);   expect(3, 0, 16#AA#);
    io_write(16#00FF#, 16#BB#);           -- odd port, no decode: no event
    -- 0x1FFC matches 0xFE (A0 = 0) and 0x1FFD: TYPE 3 then TYPE 7.
    io_write(16#1FFC#, 16#07#);           expect(3, 0, 16#07#); expect(7, 0, 16#07#);
    mem_write(16#4008#, 16#71#);          expect(2, 16#0008#, 16#71#);  -- layout 11 took effect
    io_write(16#1FFD#, 16#00#);           expect(7, 0, 16#00#);
    check("every I/O TYPE, double match, late data, /IORQ skew");

    -------------------------------------------------------------------------
    -- Inverted edge-connector clock (+2A/+3 phase).
    -------------------------------------------------------------------------
    clk_inv <= '1';
    mem_write(16#4009#, 16#81#);          expect(1, 16#0009#, 16#81#);
    io_write(16#00FE#, 16#03#, 5 ns);     expect(3, 0, 16#03#);
    io_write(16#7FFD#, 16#07#);           expect(6, 0, 16#07#);
    mem_write(16#C00A#, 16#82#);          expect(2, 16#000A#, 16#82#);
    io_write(16#7FFD#, 16#00#);           expect(6, 0, 16#00#);
    check("inverted edge-connector clock");
    clk_inv <= '0';

    -------------------------------------------------------------------------
    -- Back-to-back writes while the Pi reads asynchronously.
    -------------------------------------------------------------------------
    for i in 0 to 47 loop
      mem_write(16#4000# + i * 37, i);   expect(1, i * 37, i);
      if i mod 8 = 7 then
        io_write(16#00FE#, i);           expect(3, 0, i);
      end if;
    end loop;
    check("back-to-back writes, SOE asynchronous to the bus clock");

    -------------------------------------------------------------------------
    -- Overflow.
    -------------------------------------------------------------------------
    reader_off;
    for i in 0 to DEPTH + 2 loop
      mem_write(16#4100# + i, 16#90# + i);
      if i < DEPTH then
        expect(1, 16#0100# + i, 16#90# + i);
      end if;
    end loop;
    if nempty /= '1' then fail("overflow: NOT_EMPTY low with a full queue"); end if;
    reader_on;
    check("overflow: full queue keeps the first DEPTH events");
    mem_write(16#4200#, 16#A1#);          expect(1, 16#0200#, 16#A1#, '1');
    mem_write(16#4201#, 16#A2#);          expect(1, 16#0201#, 16#A2#, '0');
    check("overflow: OVF on the next event, then cleared");

    -------------------------------------------------------------------------
    -- SYNC.
    -------------------------------------------------------------------------
    io_write(16#7FFD#, 16#07#);           expect(6, 0, 16#07#);
    check("SYNC setup");
    reader_off;
    mem_write(16#4300#, 16#B1#);
    mem_write(16#4301#, 16#B2#);
    mem_write(16#4302#, 16#B3#);
    if nempty /= '1' then fail("SYNC: NOT_EMPTY low before SYNC"); end if;
    pulse_sync;
    if nempty /= '0' then fail("SYNC: NOT_EMPTY high after SYNC"); end if;
    reader_on;
    check("SYNC empties the queue");
    mem_write(16#C300#, 16#B4#);          expect(2, 16#0300#, 16#B4#);  -- paging kept
    check("SYNC keeps the paging state");

    -- SYNC clears the overflow flag.
    reader_off;
    for i in 0 to DEPTH + 1 loop
      mem_write(16#4400# + i, i);
    end loop;
    pulse_sync;
    reader_on;
    mem_write(16#4410#, 16#C1#);          expect(1, 16#0410#, 16#C1#, '0');
    check("SYNC clears the overflow flag");

    -- SYNC resets the half-word pointer. One extra read leaves the Pi out of
    -- step: the first half of its next pair has H = 1. The Pi pulses SYNC
    -- after that pair, with the CPLD part-way through a pair.
    reader_off;
    mem_write(16#4500#, 16#D1#);
    wait for 1 us;
    main_read(hw);                        -- the extra read
    main_read(hw);                        -- first half of the Pi's next pair
    if hw(15) /= '1' then
      fail("desync: expected H = 1 on the out-of-step read, got " & to_hstring(hw));
    end if;
    main_read(hw);                        -- second half of that pair
    if hw(15) /= '0' then
      fail("desync: expected H = 0 on the out-of-step read, got " & to_hstring(hw));
    end if;
    pulse_sync;
    main_read(hw);
    if hw /= x"0000" then
      fail("desync: first read after SYNC is not an empty half-word 0: " & to_hstring(hw));
    end if;
    main_read(hw);
    if hw /= x"8000" then
      fail("desync: second read after SYNC is not an empty half-word 1: " & to_hstring(hw));
    end if;
    reader_on;
    mem_write(16#4501#, 16#D2#);          expect(1, 16#0501#, 16#D2#);
    check("SYNC resets the half-word pointer");

    -------------------------------------------------------------------------
    -- Results.
    -------------------------------------------------------------------------
    reader_off;
    if rd_errors /= 0 then
      fail("reader reported " & integer'image(rd_errors) & " bad half-words");
    end if;
    report "reader: " & integer'image(rd_pairs) & " pairs, " &
           integer'image(rd_empties) & " empty";

    if errors = 0 then
      report "TB PASS (DEPTH = " & integer'image(DEPTH) & ")";
      std.env.finish;
    else
      report "TB FAIL (DEPTH = " & integer'image(DEPTH) & "): " &
             integer'image(errors) & " failures" severity failure;
    end if;
    wait;
  end process;

end architecture sim;
