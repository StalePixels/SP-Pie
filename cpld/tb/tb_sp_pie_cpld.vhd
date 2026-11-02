-- Self-checking testbench for sp_pie_cpld (VHDL-2008).
--
-- A Z80 bus model makes memory writes, I/O writes, I/O reads and interrupt
-- acknowledges with Zilog cycle timing. The edge-connector clock is the Z80
-- clock (or its inverse), and the testbench counts its rising edges itself.
-- A Pi model reads the queue through SMI with a jittered SOE strobe that has
-- no fixed relation to the bus clock, checks the H bit of every read word and
-- the time count of every empty pair, and records each event it receives.
-- The main process compares the received events, time count included, with
-- the expected list after each test step.
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

  -- Event KIND codes.
  constant K_MEM   : natural := 0;
  constant K_IO    : natural := 1;
  constant K_RESET : natural := 2;

  -- The design's Gray register holds the count of edge-connector clock
  -- rising edges minus this lag. The origin of the count has no meaning for
  -- the Pi; the lag only makes the testbench's own count line up with it.
  constant CNT_LAG : natural := 1;

  -- Bit positions of the time count in the 36-bit pair word (R1 & R0):
  -- T4, T3 in R0 bits 17, 16; T2 in R0 bit 9; T1, T0 in R1 bits 17, 16.
  constant T_MASK : std_logic_vector(35 downto 0) :=
    "11" & x"0000" & "11" & "000000" & "1" & "000000000";

  -- 5-bit reflected binary Gray code of n mod 32, and back.
  function gray5(n : natural) return std_logic_vector is
    variable b : unsigned(4 downto 0);
  begin
    b := to_unsigned(n mod 32, 5);
    return std_logic_vector(b xor shift_right(b, 1));
  end function;

  function bin5(g : std_logic_vector(4 downto 0)) return natural is
    variable b : unsigned(4 downto 0);
  begin
    b(4) := g(4);
    for i in 3 downto 0 loop
      b(i) := b(i + 1) xor g(i);
    end loop;
    return to_integer(b);
  end function;

  ---------------------------------------------------------------------------
  -- Event list shared by the processes.
  ---------------------------------------------------------------------------
  -- One event. For an expected event, t is the testbench count at the edge
  -- that ended the write and nrm is true when the reader was running at the
  -- write. For a received event, e is the full count the Pi gives the next
  -- empty pair and rec the full count the Pi rebuilds for the event from its
  -- T4..T0 (both -1 when the next empty pair's load edge is not known).
  type ev_t is record
    w   : std_logic_vector(35 downto 0);  -- R1 (35..18) & R0 (17..0)
    t   : integer;
    nrm : boolean;
    e   : integer;
    rec : integer;
  end record;

  type ev_list_t is protected
    procedure push(v : ev_t);
    impure function pop return ev_t;
    impure function count return natural;
    procedure clear;
  end protected;

  type ev_list_t is protected body
    type arr_t is array (0 to 1023) of ev_t;
    variable arr  : arr_t;
    variable head : natural := 0;
    variable tail : natural := 0;
    procedure push(v : ev_t) is
    begin
      arr(tail) := v;
      tail      := tail + 1;
    end procedure;
    impure function pop return ev_t is
      variable v : ev_t;
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

  -- The 36-bit word the Pi sees for one pair: R1 in bits 35..18, R0 in bits
  -- 17..0. t is the count; the word carries its Gray code.
  --   R0: T4, T3, H = 0, V = 1, OVF, F, KIND(2), T2, A15, DATA
  --   R1: T1, T0, H = 1, A14..A0
  function mk_ev(kind : natural; adr : natural; dat : natural;
                 ovf : std_logic; frm : std_logic; t : natural)
    return std_logic_vector is
    variable a16    : std_logic_vector(15 downto 0);
    variable g      : std_logic_vector(4 downto 0);
    variable r0, r1 : std_logic_vector(17 downto 0);
  begin
    a16 := std_logic_vector(to_unsigned(adr, 16));
    g   := gray5(t);
    r0  := g(4) & g(3) & '0' & '1' & ovf & frm & std_logic_vector(to_unsigned(kind, 2)) &
           g(2) & a16(15) & std_logic_vector(to_unsigned(dat, 8));
    r1  := g(1) & g(0) & '1' & a16(14 downto 0);
    return r1 & r0;
  end function;

  ---------------------------------------------------------------------------
  -- DUT connections
  ---------------------------------------------------------------------------
  signal cpu_clk  : std_logic := '0';
  signal clk_inv  : std_logic := '0';
  signal edge_clk : std_logic;
  -- Testbench count of edge-connector clock rising edges, and the time of
  -- the last one.
  signal clk_cnt  : natural := 0;
  signal clk_last : time := 0 ns;

  signal a        : std_logic_vector(15 downto 0) := (others => '1');
  signal d        : std_logic_vector(7 downto 0)  := (others => 'Z');
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
  signal rd_timed   : natural := 0;  -- empty pairs whose count was checked
  signal rd_pairs   : natural := 0;

begin

  cpu_clk  <= not cpu_clk after TCPU / 2;
  edge_clk <= cpu_clk xor clk_inv;
  soe      <= soe_r and soe_m;

  p_clk_cnt : process (edge_clk)
  begin
    if rising_edge(edge_clk) then
      clk_cnt  <= clk_cnt + 1;
      clk_last <= now;
    end if;
  end process;

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
  -- high times vary with a pseudo-random sequence. SD0..SD17 are sampled at
  -- the end of each strobe, just before SOE rises.
  --
  -- Every read word must have the right H bit. An event must not be
  -- malformed (KIND 11, or a reset event with a non-zero address or DATA).
  -- An empty pair must be exactly C4..C0 in R0 and 0x08000 in R1.
  --
  -- Time checks. The reader notes the testbench count at the SOE rising edge
  -- that ends each R1 read, which is the edge that loads the next R0.
  --   - An empty pair's C4..C0 must be the Gray code of that count. When the
  --     SOE edge comes at the same time as a clock edge, the count before or
  --     after that clock edge is accepted.
  --   - Two empty pairs in one run of reads: the step of C modulo 32 must be
  --     the step of the testbench count modulo 32, with the same tolerance.
  --   - Each event is given the full count of the next empty pair (e) and
  --     the full count the Pi rebuilds from its T4..T0 (rec): the latest
  --     count at or below e whose Gray code is T4..T0. The main process
  --     compares these with the true count of the write.
  -- The first pair of a run of reads was loaded by an edge the reader did
  -- not see (its own last read, a SYNC, or a read by the main process), so
  -- its count is not checked.
  ---------------------------------------------------------------------------
  p_reader : process
    variable lfsr   : unsigned(15 downto 0) := x"ACE1";
    variable r0, r1 : std_logic_vector(17 downto 0);
    variable errs   : natural := 0;
    variable emp    : natural := 0;
    variable timed  : natural := 0;
    variable pairs  : natural := 0;

    -- Count at the SOE edge that loaded the R0 being read, -1 if unknown,
    -- and whether that edge came at the same time as a clock edge.
    variable ld_cnt   : integer := -1;
    variable ld_coin  : boolean := false;
    variable nx_cnt   : integer;
    variable nx_coin  : boolean;
    variable dummy_c  : integer;
    variable dummy_k  : boolean;
    -- The last checked empty pair in this run: its C (binary) and count.
    variable pv_c     : integer := -1;
    variable pv_cnt   : integer := -1;
    variable pv_coin  : boolean := false;

    type pend_t is array (0 to 63) of ev_t;
    variable pend   : pend_t;
    variable npend  : natural := 0;
    variable c, dc, dt, tb_e : integer;
    variable ok     : boolean;

    procedure step_lfsr is
    begin
      lfsr := lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
    end procedure;

    -- One SMI read. cnt is the testbench count at the SOE rising edge that
    -- ends it; coin is true when a clock rising edge came at the same time.
    procedure smi_read(v : out std_logic_vector(17 downto 0);
                       cnt : out integer; coin : out boolean) is
      variable t_rise : time;
    begin
      step_lfsr;
      soe_r <= '0';
      wait for 37 ns + (to_integer(lfsr(4 downto 0)) * 1 ns);
      v := sd17 & sd16 & sd;
      soe_r  <= '1';
      t_rise := now;
      cnt    := clk_cnt - CNT_LAG;
      step_lfsr;
      wait for 29 ns + (to_integer(lfsr(5 downto 0)) * 1 ns);
      coin := clk_last = t_rise;
    end procedure;

    procedure flush(e : integer; c_bin : integer) is
      variable x : ev_t;
      variable tb : natural;
    begin
      for i in 0 to npend - 1 loop
        x := pend(i);
        if e >= 0 then
          tb    := bin5(x.w(17 downto 16) & x.w(9) & x.w(35 downto 34));
          x.e   := e;
          x.rec := e - ((c_bin - tb) mod 32);
        end if;
        got.push(x);
      end loop;
      npend := 0;
    end procedure;

    impure function near(a, b : integer; tol : boolean) return boolean is
    begin
      if tol then
        return (a - b) mod 32 <= 1 or (b - a) mod 32 <= 1;
      end if;
      return (a - b) mod 32 = 0;
    end function;

  begin
    loop
      if rd_en = '0' then
        flush(-1, 0);
        ld_cnt := -1;
        pv_c   := -1;
        rd_idle <= '1';
        wait until rd_en = '1';
      end if;
      rd_idle <= '0';
      smi_read(r0, dummy_c, dummy_k);
      smi_read(r1, nx_cnt, nx_coin);
      pairs := pairs + 1;
      if r0(15) /= '0' or r1(15) /= '1' then
        report "reader: H bit wrong, R0=" & to_hstring(r0) & " R1=" & to_hstring(r1)
          severity error;
        errs := errs + 1;
      elsif r0(14) = '1' then
        if r0(11 downto 10) = "11" or
           (r0(11 downto 10) = "10" and
            (r0(8 downto 0) /= "000000000" or r1(14 downto 0) /= "000000000000000")) then
          report "reader: malformed event, R0=" & to_hstring(r0) & " R1=" & to_hstring(r1)
            severity error;
          errs := errs + 1;
        end if;
        pend(npend) := (w => r1 & r0, t => -1, nrm => false, e => -1, rec => -1);
        npend := npend + 1;
      elsif r0(17 downto 5) /= "0000000000000" or r1 /= "00" & x"8000" then
        report "reader: empty pair not clean, R0=" & to_hstring(r0) & " R1=" & to_hstring(r1)
          severity error;
        errs := errs + 1;
      else
        emp := emp + 1;
        c   := bin5(r0(4 downto 0));
        if ld_cnt >= 0 then
          timed := timed + 1;
          ok    := near(c, ld_cnt, ld_coin);
          if not ok then
            report "reader: empty pair count C=" & integer'image(c) & ", expected " &
                   integer'image(ld_cnt mod 32) severity error;
            errs := errs + 1;
          end if;
          if pv_c >= 0 then
            dc := (c - pv_c) mod 32;
            dt := (ld_cnt - pv_cnt) mod 32;
            if not near(dc, dt, ld_coin or pv_coin) then
              report "reader: empty pair count step " & integer'image(dc) &
                     ", clock edges " & integer'image(dt) severity error;
              errs := errs + 1;
            end if;
          end if;
          -- The Pi's full count for this empty pair: the testbench count,
          -- moved to the value that C4..C0 actually names.
          dc := (c - ld_cnt) mod 32;
          if dc > 16 then
            dc := dc - 32;
          end if;
          tb_e := ld_cnt + dc;
          flush(tb_e, c);
          pv_c    := c;
          pv_cnt  := ld_cnt;
          pv_coin := ld_coin;
        else
          flush(-1, 0);
          pv_c := -1;
        end if;
      end if;
      ld_cnt  := nx_cnt;
      ld_coin := nx_coin;
      rd_errors  <= errs;
      rd_empties <= emp;
      rd_timed   <= timed;
      rd_pairs   <= pairs;
    end loop;
  end process;

  ---------------------------------------------------------------------------
  -- Main test sequence.
  ---------------------------------------------------------------------------
  p_main : process
    -- /RESET after the design's filter. The reset event's count is the count
    -- at its rising edge.
    alias rst_n_dut is << signal .tb_sp_pie_cpld.dut.rst_n : std_logic >>;

    variable errors : natural := 0;
    variable placed : natural := 0;  -- events whose rebuilt count was checked
    variable e0, e1 : natural;
    variable rnd    : unsigned(31 downto 0) := x"1D872B41";

    procedure fail(msg : string) is
    begin
      report "FAIL: " & msg severity error;
      errors := errors + 1;
    end procedure;

    -- Pseudo-random number, 32-bit LFSR.
    impure function rand16 return natural is
    begin
      for i in 0 to 15 loop
        rnd := rnd(30 downto 0) & (rnd(31) xor rnd(21) xor rnd(1) xor rnd(0));
      end loop;
      return to_integer(rnd(15 downto 0));
    end function;

    -- Memory write cycle (Zilog UM0080 figure 6): address in T1, /MREQ and
    -- data from the middle of T1, /WR low from the falling edge of T2 to the
    -- falling edge of T3. Two calls in a row give two writes three T-states
    -- apart, the rate of the two stack writes of PUSH, CALL or an interrupt.
    -- t is the testbench count at the /WR rising edge.
    procedure mem_write(addr : natural; dat : natural; t : out natural) is
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
      d      <= transport (others => 'Z') after T_CTRL + T_HOLD;
      a      <= transport (others => 'X') after TCPU / 2 + T_ADDR;
      wait until wr_n = '1';
      t := clk_cnt - CNT_LAG;
    end procedure;

    procedure mem_write(addr : natural; dat : natural) is
      variable t : natural;
    begin
      mem_write(addr, dat, t);
    end procedure;

    -- I/O write cycle (Zilog UM0080 figure 7): /IORQ and /WR fall after the
    -- rising edge of T2, one automatic wait state, both rise at the falling
    -- edge of T3. iorq_skew moves /IORQ against /WR (positive: /IORQ later).
    -- The data bus carries the inverted byte until T_IO_LATE after /WR falls.
    -- t is the testbench count at the /WR rising edge.
    procedure io_write(addr : natural; dat : natural; iorq_skew : time;
                       t : out natural) is
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
      d      <= transport (others => 'Z') after T_CTRL + T_HOLD;
      a      <= transport (others => 'X') after TCPU / 2 + T_ADDR;
      wait until wr_n = '1';
      t := clk_cnt - CNT_LAG;
    end procedure;

    procedure io_write(addr : natural; dat : natural; iorq_skew : time := 0 ns) is
      variable t : natural;
    begin
      io_write(addr, dat, iorq_skew, t);
    end procedure;

    -- I/O read cycle (Zilog UM0080 figure 7): /IORQ and /RD fall after the
    -- rising edge of T2, one automatic wait state, the CPU samples the data
    -- bus at the falling edge of T3, and /IORQ and /RD rise at that edge.
    procedure io_read(addr : natural; v : out std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(cpu_clk);                      -- T1
      a <= transport std_logic_vector(to_unsigned(addr, 16)) after T_ADDR;
      wait until rising_edge(cpu_clk);                      -- T2
      rd_n   <= transport '0' after T_CTRL;
      iorq_n <= transport '0' after T_CTRL;
      wait until rising_edge(cpu_clk);                      -- TW
      wait until rising_edge(cpu_clk);                      -- T3
      wait until falling_edge(cpu_clk);
      v := d;
      rd_n   <= transport '1' after T_CTRL;
      iorq_n <= transport '1' after T_CTRL;
      a      <= transport (others => 'X') after TCPU / 2 + T_ADDR;
    end procedure;

    -- Interrupt acknowledge (Zilog UM0080 figure 10): /M1 low from T1, /IORQ
    -- low after two automatic wait states, both rise at the end of T3. /RD
    -- and /WR stay high.
    procedure int_ack is
    begin
      wait until rising_edge(cpu_clk);                      -- T1
      a    <= transport x"1234" after T_ADDR;
      m1_n <= transport '0' after T_CTRL;
      wait until rising_edge(cpu_clk);                      -- T2
      wait until rising_edge(cpu_clk);                      -- TW1
      wait until falling_edge(cpu_clk);
      iorq_n <= transport '0' after T_CTRL;
      wait until rising_edge(cpu_clk);                      -- TW2
      wait until rising_edge(cpu_clk);                      -- T3
      m1_n   <= transport '1' after T_CTRL;
      iorq_n <= transport '1' after T_CTRL;
      a      <= transport (others => 'X') after TCPU / 2 + T_ADDR;
    end procedure;

    -- An expected event. t is its count; nrm is true when the reader runs.
    procedure exp_ev(kind : natural; adr : natural; dat : natural;
                     ovf : std_logic; frm : std_logic; t : natural) is
    begin
      exp.push((w => mk_ev(kind, adr, dat, ovf, frm, t), t => t,
                nrm => rd_en = '1', e => -1, rec => -1));
    end procedure;

    procedure exp_mem(adr : natural; dat : natural; t : natural;
                      ovf : std_logic := '0'; frm : std_logic := '0') is
    begin
      exp_ev(K_MEM, adr, dat, ovf, frm, t);
    end procedure;

    procedure exp_rst(t : natural; ovf : std_logic := '0'; frm : std_logic := '0') is
    begin
      exp_ev(K_RESET, 0, 0, ovf, frm, t);
    end procedure;

    -- A memory write, and the event it must give.
    procedure mw(addr : natural; dat : natural; ovf : std_logic := '0';
                 frm : std_logic := '0') is
      variable t : natural;
    begin
      mem_write(addr, dat, t);
      exp_mem(addr, dat, t, ovf, frm);
    end procedure;

    -- An I/O write, and the event it must give.
    procedure iw(addr : natural; dat : natural; skew : time := 0 ns;
                 ovf : std_logic := '0'; frm : std_logic := '0') is
      variable t : natural;
    begin
      io_write(addr, dat, skew, t);
      exp_ev(K_IO, addr, dat, ovf, frm, t);
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
    -- The fields must match exactly, the time count T4..T0 too. For an event
    -- written while the reader runs, the next empty pair must come at most
    -- 31 clocks after the write (its count e minus the write's count is 0 to
    -- 31), and the count the Pi rebuilds from T4..T0 must be the write's.
    procedure check(name : string) is
      variable g, x : ev_t;
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
        if (g.w and not T_MASK) /= (x.w and not T_MASK) then
          fail(name & ": got " & to_hstring(g.w) & ", expected " & to_hstring(x.w));
          ok := false;
        else
          if (g.w and T_MASK) /= (x.w and T_MASK) then
            fail(name & ": event time count wrong, got " & to_hstring(g.w) &
                 ", expected " & to_hstring(x.w));
            ok := false;
          end if;
          if x.nrm and g.e >= 0 then
            placed := placed + 1;
            if g.e < x.t or g.e - x.t >= 32 then
              fail(name & ": next empty pair at count " & integer'image(g.e) &
                   ", event at " & integer'image(x.t));
              ok := false;
            elsif g.rec /= x.t then
              fail(name & ": Pi rebuilds count " & integer'image(g.rec) &
                   ", event at " & integer'image(x.t));
              ok := false;
            end if;
          end if;
        end if;
      end loop;
      while got.count > 0 loop
        fail(name & ": extra event " & to_hstring(got.pop.w));
        ok := false;
      end loop;
      while exp.count > 0 loop
        fail(name & ": missing event " & to_hstring(exp.pop.w));
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

    -- Let the reader run for a while on an empty queue. Its empty pairs must
    -- carry the testbench count: the reader checks each one, and this step
    -- fails when it reports a new error or checks too few.
    procedure check_counts(name : string) is
      variable e0, t0 : natural;
    begin
      e0 := rd_errors;
      t0 := rd_timed;
      wait for 4 us;
      if rd_errors /= e0 then
        fail(name & ": reader reported " & integer'image(rd_errors - e0) & " errors");
      elsif rd_timed < t0 + 5 then
        fail(name & ": only " & integer'image(rd_timed - t0) & " empty pair counts checked");
      else
        report "ok   " & name;
      end if;
    end procedure;

    -- A Spectrum reset. The address and data buses carry non-zero values
    -- while /RESET is low and as it goes high, so a reset event that took
    -- them from the bus would fail the address 0 and DATA 0 checks. t is the
    -- testbench count when the filtered /RESET goes high, the edge that
    -- stores the reset event.
    procedure pulse_reset(t : out natural) is
    begin
      wait until rising_edge(cpu_clk);
      reset_n <= '0';
      a       <= x"DA5A";
      d       <= x"A5";
      wait for 8 * TCPU;
      reset_n <= '1';
      wait until rst_n_dut = '1' for 8 * TCPU;
      if rst_n_dut /= '1' then
        fail("filtered /RESET did not go high");
      end if;
      t := clk_cnt - CNT_LAG;
      wait for 4 * TCPU;
      a       <= (others => 'X');
      d       <= (others => 'Z');
    end procedure;

    procedure pulse_reset is
      variable t : natural;
    begin
      pulse_reset(t);
    end procedure;

    -- One SMI read driven by the main process, for the desync test.
    procedure main_read(v : out std_logic_vector(17 downto 0)) is
    begin
      soe_m <= '0';
      wait for 50 ns;
      v := sd17 & sd16 & sd;
      soe_m <= '1';
      wait for 50 ns;
    end procedure;

    -- Fill the queue with DEPTH memory writes while the reader is off.
    procedure fill_queue(base : natural) is
    begin
      for i in 0 to DEPTH - 1 loop
        mw(base + i, (16#E0# + i) mod 256);
      end loop;
    end procedure;

    variable hw     : std_logic_vector(17 downto 0);
    variable rb     : std_logic_vector(7 downto 0);
    variable ad, dt : natural;
    variable tr     : natural;

  begin
    report "testbench DEPTH = " & integer'image(DEPTH);

    -- Power-up with /RESET low.
    wait for 3 us;
    reset_n <= '1';
    wait until rst_n_dut = '1';
    tr := clk_cnt - CNT_LAG;
    wait for 3 us;

    -- Static outputs.
    if romcs /= 'Z' then fail("/ROMCS is not high impedance"); end if;
    if sd16 /= '0' or sd17 /= '0' then fail("SD16/SD17 not low before the first read"); end if;
    if d /= "ZZZZZZZZ" then fail("D0-D7 driven at power-up: " & to_hstring(d)); end if;

    -------------------------------------------------------------------------
    -- Power-up: /RESET going high queued one reset event.
    -------------------------------------------------------------------------
    exp_rst(tr);
    reader_on;
    check("power-up /RESET high queues one reset event");

    -------------------------------------------------------------------------
    -- Empty queue: only clean empty pairs.
    -------------------------------------------------------------------------
    e0 := rd_empties;
    wait for 3 us;
    e1 := rd_empties;
    if e1 - e0 < 10 then
      fail("empty queue: expected a stream of empty pairs, got " & integer'image(e1 - e0));
    end if;
    if nempty /= '0' then fail("empty queue: NOT_EMPTY high"); end if;
    check("empty queue gives empty pairs (count in R0, 0x08000)");

    -------------------------------------------------------------------------
    -- Memory writes anywhere in 0x0000-0xFFFF, exact address and DATA.
    -------------------------------------------------------------------------
    mw(16#0000#, 16#00#);
    mw(16#FFFF#, 16#FF#);
    mw(16#3FFF#, 16#5A#);
    mw(16#4000#, 16#A5#);
    mw(16#5AFF#, 16#3C#);
    mw(16#5B00#, 16#C3#);
    mw(16#7FFF#, 16#81#);
    mw(16#8000#, 16#7E#);
    mw(16#C000#, 16#01#);
    mw(16#DAFF#, 16#80#);
    check("memory writes: ROM area, screen, every 16 KB slot");

    -- Walking one and walking zero on every address and data line.
    for i in 0 to 15 loop
      mw(2 ** i, 2 ** (i mod 8));
      mw(16#FFFF# - 2 ** i, 255 - 2 ** (i mod 8));
    end loop;
    check("memory writes: walking one and zero on A15..A0 and D7..D0");

    -------------------------------------------------------------------------
    -- I/O writes to any port, full 16-bit address, late data, /IORQ skew.
    -------------------------------------------------------------------------
    iw(16#00FE#, 16#02#);
    iw(16#FFFE#, 16#AA#, -15 ns);
    iw(16#7FFD#, 16#10#, -10 ns);
    iw(16#1FFD#, 16#04#, 15 ns);
    iw(16#1FFC#, 16#07#);
    iw(16#BF3B#, 16#40#);
    iw(16#FF3B#, 16#01#, 10 ns);
    iw(16#FFFD#, 16#0E#);
    iw(16#BFFD#, 16#55#);
    iw(16#00FF#, 16#BB#);
    iw(16#0000#, 16#00#);
    iw(16#FFFF#, 16#FF#);
    check("I/O writes: common ports and odd ports, full address");

    for i in 0 to 15 loop
      iw(2 ** i, 2 ** (i mod 8));
      iw(16#FFFF# - 2 ** i, 255 - 2 ** (i mod 8), 5 ns);
    end loop;
    check("I/O writes: walking one and zero on A15..A0 and D7..D0");

    -- Reads and acknowledges are not events.
    io_read(16#00FE#, rb);
    io_read(16#7FFD#, rb);
    io_read(16#FFFD#, rb);
    int_ack;
    mw(16#4321#, 16#12#, '0', '1');   -- F from the acknowledge above
    check("I/O reads and acknowledges are not events");

    -------------------------------------------------------------------------
    -- Inverted edge-connector clock (+2A/+3 phase).
    -------------------------------------------------------------------------
    clk_inv <= '1';
    mw(16#4009#, 16#81#);
    iw(16#00FE#, 16#03#, 5 ns);
    iw(16#7FFD#, 16#07#);
    mw(16#C00A#, 16#82#);
    iw(16#1FFD#, 16#04#, -5 ns);
    check("inverted edge-connector clock");
    clk_inv <= '0';

    -------------------------------------------------------------------------
    -- Reset events.
    -------------------------------------------------------------------------
    pulse_reset(tr);              exp_rst(tr);
    mw(16#C005#, 16#61#);
    pulse_reset(tr);              exp_rst(tr);
    iw(16#7FFD#, 16#07#);
    check("one reset event per reset, address 0 and DATA 0");

    -- Order: after every event from before the reset, before every event
    -- after it. The reader is off, so all of them wait in the queue together.
    reader_off;
    mw(16#C00B#, 16#65#);
    pulse_reset(tr);              exp_rst(tr);
    if DEPTH >= 4 then
      mw(16#C00C#, 16#66#);
      iw(16#00FE#, 16#04#);
    end if;
    reader_on;
    check("reset event comes after earlier events and before later ones");

    -- Reset while the queue is full: the reset event is dropped like any
    -- other event, and the next stored event carries OVF = 1.
    reader_off;
    fill_queue(16#4600#);
    pulse_reset;                                         -- queue full: dropped
    reader_on;
    check("reset while full: the DEPTH earlier events arrive");
    iw(16#00FE#, 16#01#, 0 ns, '1');
    iw(16#00FE#, 16#02#);
    check("reset while full: OVF on the next event");

    -------------------------------------------------------------------------
    -- Frame flag F.
    -------------------------------------------------------------------------
    -- The first event after an acknowledge carries F; the next does not.
    int_ack;
    mw(16#7FFE#, 16#12#, '0', '1');   -- return-address push, high byte
    mw(16#7FFD#, 16#34#);             -- low byte
    mw(16#4000#, 16#E1#);
    check("F on the first event after an interrupt acknowledge");

    -- Two acknowledges before a write give one F.
    int_ack;
    int_ack;
    iw(16#00FE#, 16#03#, 0 ns, '0', '1');
    iw(16#00FE#, 16#04#);
    check("two acknowledges, one F");

    -- An acknowledge before a reset puts F on the reset event.
    int_ack;
    pulse_reset(tr);              exp_rst(tr, '0', '1');
    mw(16#5800#, 16#38#);
    check("F on a reset event after an acknowledge");

    -- An event dropped on a full queue leaves F pending for the next stored
    -- event, which also has OVF = 1.
    reader_off;
    fill_queue(16#4700#);
    int_ack;
    mem_write(16#4800#, 16#F0#);      -- dropped
    mem_write(16#4801#, 16#F1#);      -- dropped
    reader_on;
    check("F setup: full queue keeps the first DEPTH events");
    mw(16#4802#, 16#F2#, '1', '1');
    mw(16#4803#, 16#F3#);
    check("F kept across dropped events, with OVF");

    -------------------------------------------------------------------------
    -- Back-to-back writes while the Pi reads asynchronously. Pairs of memory
    -- writes three T-states apart (stack pushes), with I/O writes between.
    -------------------------------------------------------------------------
    for i in 0 to 63 loop
      ad := rand16;
      dt := rand16 mod 256;
      mw(ad, dt);
      mw((ad + 16#FFFF#) mod 16#10000#, 255 - dt);
      if i mod 4 = 3 then
        iw(rand16, rand16 mod 256);
      end if;
    end loop;
    check("back-to-back writes, SOE asynchronous to the bus clock");

    -------------------------------------------------------------------------
    -- Overflow.
    -------------------------------------------------------------------------
    reader_off;
    for i in 0 to DEPTH + 2 loop
      mem_write(16#9100# + i, 16#90# + i, tr);
      if i < DEPTH then
        exp_mem(16#9100# + i, 16#90# + i, tr);
      end if;
    end loop;
    if nempty /= '1' then fail("overflow: NOT_EMPTY low with a full queue"); end if;
    reader_on;
    check("overflow: full queue keeps the first DEPTH events");
    mw(16#4200#, 16#A1#, '1');
    mw(16#4201#, 16#A2#, '0');
    check("overflow: OVF on the next event, then cleared");

    -------------------------------------------------------------------------
    -- SYNC.
    -------------------------------------------------------------------------
    reader_off;
    mem_write(16#4300#, 16#B1#);
    mem_write(16#4301#, 16#B2#);
    io_write(16#4302#, 16#B3#);
    if nempty /= '1' then fail("SYNC: NOT_EMPTY low before SYNC"); end if;
    pulse_sync;
    if nempty /= '0' then fail("SYNC: NOT_EMPTY high after SYNC"); end if;
    reader_on;
    check("SYNC empties the queue");

    -- SYNC clears the overflow flag.
    reader_off;
    for i in 0 to DEPTH + 1 loop
      mem_write(16#4400# + i, i);
    end loop;
    pulse_sync;
    reader_on;
    mw(16#4410#, 16#C1#, '0');
    check("SYNC clears the overflow flag");

    -- SYNC resets the read-word pointer. One extra read leaves the Pi out of
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
    if hw /= "00" & x"0000" then
      fail("desync: first read after SYNC is not the cleared empty R0: " & to_hstring(hw));
    end if;
    main_read(hw);
    if hw /= "00" & x"8000" then
      fail("desync: second read after SYNC is not an empty R1: " & to_hstring(hw));
    end if;
    reader_on;
    mw(16#4501#, 16#D2#);
    check("SYNC resets the read-word pointer");

    -- The time count runs on through SYNC. Two SYNCs a few clocks apart, so
    -- a counter that SYNC cleared could not match the testbench count by
    -- chance after both.
    for i in 1 to 2 loop
      reader_off;
      wait for i * 3 * TCPU;
      pulse_sync;
      wait for 1 us;
      reader_on;
      check_counts("time count not reset by SYNC (" & integer'image(i) & ")");
    end loop;

    -------------------------------------------------------------------------
    -- ULAplus read-back of port 0xFF3B. With the mode group selected the read
    -- returns the mode register, otherwise the last byte written to 0xFF3B.
    -- The writes are still events. Other ports, and the bus between reads,
    -- stay high impedance.
    -------------------------------------------------------------------------
    iw(16#BF3B#, 16#40#);                 -- mode group
    iw(16#FF3B#, 16#01#);                 -- mode register = 01
    io_read(16#FF3B#, rb);
    if rb /= x"01" then fail("ULAplus: mode read-back " & to_hstring(rb) & ", expected 01"); end if;
    iw(16#BF3B#, 16#05#);                 -- palette group
    iw(16#FF3B#, 16#9C#);
    io_read(16#FF3B#, rb);
    if rb /= x"9C" then fail("ULAplus: data read-back " & to_hstring(rb) & ", expected 9C"); end if;
    iw(16#BF3B#, 16#40#);                 -- mode group again
    io_read(16#FF3B#, rb);
    if rb /= x"01" then fail("ULAplus: mode not kept, read " & to_hstring(rb)); end if;
    iw(16#BF3B#, 16#C0#);                 -- group 11: not the mode group
    io_read(16#FF3B#, rb);
    if rb /= x"9C" then fail("ULAplus: group 11 read-back " & to_hstring(rb) & ", expected 9C"); end if;
    mw(16#FF3B#, 16#77#);                 -- memory write to the same address
    io_read(16#FF3B#, rb);
    if rb /= x"9C" then fail("ULAplus: memory write changed read-back " & to_hstring(rb)); end if;
    io_read(16#BF3B#, rb);
    if rb /= "ZZZZZZZZ" then fail("ULAplus: read of 0xBF3B drove the bus: " & to_hstring(rb)); end if;
    io_read(16#FFFD#, rb);
    if rb /= "ZZZZZZZZ" then fail("ULAplus: read of 0xFFFD drove the bus: " & to_hstring(rb)); end if;
    io_read(16#FF3A#, rb);
    if rb /= "ZZZZZZZZ" then fail("ULAplus: read of 0xFF3A drove the bus: " & to_hstring(rb)); end if;
    io_read(16#7F3B#, rb);
    if rb /= "ZZZZZZZZ" then fail("ULAplus: read of 0x7F3B drove the bus: " & to_hstring(rb)); end if;
    wait for TCPU;
    if d /= "ZZZZZZZZ" then fail("ULAplus: bus driven after the read: " & to_hstring(d)); end if;
    check("ULAplus read-back");

    iw(16#BF3B#, 16#40#);
    iw(16#FF3B#, 16#3F#);
    pulse_reset(tr);                      exp_rst(tr);
    io_read(16#FF3B#, rb);
    if rb /= x"00" then fail("ULAplus: read-back after reset " & to_hstring(rb) & ", expected 00"); end if;
    iw(16#FF3B#, 16#12#);                 -- group cleared: this is not the mode
    iw(16#BF3B#, 16#40#);
    io_read(16#FF3B#, rb);
    if rb /= x"00" then fail("ULAplus: mode after reset " & to_hstring(rb) & ", expected 00"); end if;
    check("ULAplus registers cleared by /RESET");

    -- /RESET filter: low pulses shorter than four edge-connector clocks are
    -- not resets. No reset event, and writes before and after them arrive.
    reader_on;
    mw(16#4123#, 16#5A#);
    for i in 1 to 5 loop
      wait until rising_edge(cpu_clk);
      reset_n <= '0';
      wait for 50 ns * i;
      reset_n <= '1';
      wait for 6 * TCPU;
    end loop;
    wait until rising_edge(cpu_clk);
    reset_n <= '0';
    wait for 2 * TCPU;
    reset_n <= '1';
    wait for 2 * TCPU;
    mw(16#4124#, 16#A5#);
    check("/RESET filter: short pulses give no reset event");

    -- The time count runs on through a Spectrum reset, with the reader
    -- running. Two resets a few clocks apart, as for SYNC above.
    for i in 1 to 2 loop
      wait for i * 3 * TCPU;
      pulse_reset(tr);              exp_rst(tr);
      check("reset event during the time count test (" & integer'image(i) & ")");
      check_counts("time count not reset by /RESET (" & integer'image(i) & ")");
    end loop;

    -------------------------------------------------------------------------
    -- Results.
    -------------------------------------------------------------------------
    reader_off;
    if rd_errors /= 0 then
      fail("reader reported " & integer'image(rd_errors) & " bad read words");
    end if;
    if rd_timed = 0 then fail("no empty pair count was checked"); end if;
    if placed = 0 then fail("no event count was rebuilt from an empty pair"); end if;
    report "reader: " & integer'image(rd_pairs) & " pairs, " &
           integer'image(rd_empties) & " empty, " & integer'image(rd_timed) &
           " empty counts checked, " & integer'image(placed) & " events placed";

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
