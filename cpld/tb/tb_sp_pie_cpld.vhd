-- Self-checking testbench for sp_pie_cpld (VHDL-2008).
--
-- A Z80 bus model makes memory writes, I/O writes, I/O reads and interrupt
-- acknowledges with Zilog cycle timing. The edge-connector clock is the Z80
-- clock (or its inverse). The clock model can stretch high or low phases by
-- whole T-states, as the ULA does during contention. A Pi model drives FCLK
-- (25 MHz with phase jitter, no fixed relation to the Z80 clock) when it is
-- on, and reads the queue through SMI with a jittered SOE strobe that has no
-- fixed relation to either clock. It checks the H bit of every read word,
-- the time count and STL of every empty pair, and records each event it
-- receives. The main process compares the received events, time count
-- included, with the expected list after each test step.
--
-- The testbench keeps its own counts: the rising edges of the edge-connector
-- clock, and ULA time (those edges plus every stall T-state). Without FCLK
-- the design must give the edge count; with FCLK, ULA time.
--
-- The tests run as one suite, repeated with different clocks: no FCLK; FCLK
-- with stalls in the high phase; FCLK with stalls in the low phase; FCLK
-- without stalls; and no FCLK again, with stalls.
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

  -- FCLK: 25 MHz. Each rising edge comes at a multiple of 40 ns from its
  -- start, moved by up to FCLK_JIT either way.
  constant TF       : time := 40 ns;
  constant FCLK_JIT : natural := 2;  -- ns

  -- With FCLK, a CLK edge or a stall T-state reaches the design's count
  -- between LAG_MIN and LAG_MAX after the testbench counts it (three FCLK
  -- periods at most, plus jitter). The testbench counts stall T-state k of a
  -- stalled phase at k * 7 FCLK periods after the edge that started the
  -- phase, where the design's threshold is.
  constant LAG_MIN : time := 72 ns;
  constant LAG_MAX : time := 128 ns;
  -- STL: the design's stall pulse starts with the count step and lasts one
  -- FCLK period.
  constant PS_MIN  : time := LAG_MIN;
  constant PS_MAX  : time := LAG_MAX;
  constant PE_MIN  : time := PS_MIN + 36 ns;
  constant PE_MAX  : time := PS_MAX + 44 ns;
  constant EPS     : time := 3 ns;

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
  -- One event. For an expected event, lo to hi is the window of the count
  -- at the edge that ended the write (one value except when a count step
  -- comes at that edge) and nrm is true when the reader was running at the
  -- write. For a received event, e is the full count the Pi gives the next
  -- empty pair and rec the full count the Pi rebuilds for the event from its
  -- T4..T0 (both -1 when the next empty pair's load edge is not known).
  type ev_t is record
    w   : std_logic_vector(35 downto 0);  -- R1 (35..18) & R0 (17..0)
    lo  : integer;
    hi  : integer;
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

  -- History of count steps: the time of each step. at(t) is the count at
  -- time t (steps at or before t). any_in(a, b) is true when a step lies
  -- between a and b.
  type hist_t is protected
    procedure push(t : time);
    impure function at(t : time) return integer;
    impure function any_in(a, b : time) return boolean;
  end protected;

  type hist_t is protected body
    constant N : natural := 512;
    type arr_t is array (0 to N - 1) of time;
    variable arr : arr_t;
    variable cnt : integer := 0;
    procedure push(t : time) is
    begin
      arr(cnt mod N) := t;
      cnt := cnt + 1;
    end procedure;
    impure function at(t : time) return integer is
      variable i : integer := cnt - 1;
    begin
      while i >= 0 and i > cnt - N loop
        exit when arr(i mod N) <= t;
        i := i - 1;
      end loop;
      assert i > cnt - N or cnt < N report "hist: time too old" severity failure;
      return i + 1;
    end function;
    impure function any_in(a, b : time) return boolean is
      variable i : integer := cnt - 1;
    begin
      while i >= 0 and i > cnt - N loop
        if arr(i mod N) >= a and arr(i mod N) <= b then
          return true;
        end if;
        exit when arr(i mod N) < a;
        i := i - 1;
      end loop;
      return false;
    end function;
  end protected body;

  shared variable h_edge  : hist_t;  -- edge-connector clock rising edges
  shared variable h_ula   : hist_t;  -- those edges and every stall T-state
  shared variable h_stall : hist_t;  -- stall T-states only

  -- The count window for a sample of the design's count taken at time t:
  -- lo to hi (full counts). Mode 0 (no FCLK): the edge count, with an edge
  -- at the same time as t counted or not. Mode 1 (FCLK): ULA time, LAG_MIN
  -- to LAG_MAX late. off lines the testbench count up with the design's.
  procedure window(t : time; mode : natural; off : integer;
                   lo, hi : out integer) is
  begin
    if mode = 0 then
      lo := h_edge.at(t - 1 ps) + off;
      hi := h_edge.at(t) + off;
    else
      lo := h_ula.at(t - LAG_MAX) + off;
      hi := h_ula.at(t - LAG_MIN) + off;
    end if;
  end procedure;

  -- The 36-bit word the Pi sees for one pair: R1 in bits 35..18, R0 in bits
  -- 17..0. t is the count (any integer); the word carries its Gray code.
  --   R0: T4, T3, H = 0, V = 1, OVF, F, KIND(2), T2, A15, DATA
  --   R1: T1, T0, H = 1, A14..A0
  function mk_ev(kind : natural; adr : natural; dat : natural;
                 ovf : std_logic; frm : std_logic; t : integer)
    return std_logic_vector is
    variable a16    : std_logic_vector(15 downto 0);
    variable g      : std_logic_vector(4 downto 0);
    variable r0, r1 : std_logic_vector(17 downto 0);
  begin
    a16 := std_logic_vector(to_unsigned(adr, 16));
    g   := gray5(t mod 32);
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
  -- Stalls: while st_on is 1, about one phase in four at the level st_lvl
  -- of the edge-connector clock is longer by 1 to 6 T-states.
  signal st_on    : std_logic := '0';
  signal st_lvl   : std_logic := '1';
  -- Stalled phases made, per number of stall T-states.
  type kcount_t is array (1 to 6) of natural;
  signal st_k     : kcount_t := (others => 0);

  -- FCLK from the Pi, on while fclk_on is 1. When off, GPIO 4 is an input
  -- with its pull-up, so the line is high.
  signal fclk_on  : std_logic := '0';
  signal fclk     : std_logic := '1';

  -- Which count the design must give (0: edge count, 1: ULA time), and the
  -- offset from the testbench count. Both change only while the reader is
  -- off; tm_ok is 0 while the design switches.
  signal tmode    : natural := 0;
  signal toff     : integer := 0;
  signal tm_ok    : std_logic := '1';

  signal a        : std_logic_vector(15 downto 0) := (others => '1');
  signal d        : std_logic_vector(7 downto 0)  := (others => 'Z');
  signal mreq_n   : std_logic := '1';
  signal iorq_n   : std_logic := '1';
  signal rd_n     : std_logic := '1';
  signal wr_n     : std_logic := '1';
  signal reset_n  : std_logic := '0';
  signal m1_n     : std_logic := '1';
  signal romcs    : std_logic;

  signal sa       : std_logic_vector(5 downto 2) := (others => '0');
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
  signal rd_stl     : natural := 0;  -- empty pairs with STL = 1
  signal rd_stlchk  : natural := 0;  -- empty pairs whose STL was checked
  signal rd_stlmust : natural := 0;  -- of those, pairs that had to have STL = 1
  signal gray_err   : natural := 0;  -- count steps that were not one Gray step

begin

  edge_clk <= cpu_clk xor clk_inv;
  soe      <= soe_r and soe_m;

  -- Z80 clock. Each phase lasts TCPU / 2. While st_on is 1, a phase of the
  -- edge-connector clock at the level st_lvl is longer by 1 to 6 whole
  -- T-states one time in four (pseudo-random), never two phases in a row.
  -- Stall T-state k of a phase that starts at ps is counted in ULA time at
  -- ps + k * 7 * TF.
  p_cpu_clk : process
    variable lfsr : unsigned(15 downto 0) := x"B5C3";
    variable k    : natural;
    variable ps   : time;
    variable last : boolean := false;
    variable cv   : std_logic := '0';
  begin
    wait for TCPU / 2;
    cv := not cv;
    cpu_clk <= cv;
    ps := now;
    k  := 0;
    for i in 1 to 8 loop
      lfsr := lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
    end loop;
    if st_on = '1' and (cv xor clk_inv) = st_lvl and not last and lfsr(1 downto 0) = "00" then
      k := 1 + to_integer(lfsr(7 downto 2)) mod 6;
      st_k(k) <= st_k(k) + 1;
    end if;
    last := k > 0;
    for i in 1 to k loop
      wait for ps + i * 7 * TF - now;
      h_ula.push(now);
      h_stall.push(now);
    end loop;
    if k > 0 then
      wait for ps + k * TCPU - now;
    end if;
  end process;

  -- Testbench edge count and ULA time: each edge-connector clock rising edge.
  p_clk_cnt : process (edge_clk)
  begin
    if rising_edge(edge_clk) then
      h_edge.push(now);
      h_ula.push(now);
    end if;
  end process;

  -- FCLK. Rising edges at start + n * TF, each moved by up to FCLK_JIT ns.
  p_fclk : process
    variable lfsr : unsigned(15 downto 0) := x"6A3D";
    variable t0   : time;
    variable n    : natural := 0;
    variable j    : integer;
  begin
    if fclk_on = '0' then
      fclk <= '1';
      wait until fclk_on = '1';
      t0 := now + 13.7 ns;
      n  := 0;
    end if;
    for i in 1 to 3 loop
      lfsr := lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
    end loop;
    j := to_integer(lfsr(2 downto 0)) mod (2 * FCLK_JIT + 1) - FCLK_JIT;
    wait for t0 + n * TF + j * 1 ns - now;
    fclk <= '1';
    wait for TF / 2;
    fclk <= '0';
    n := n + 1;
  end process;

  -- The design's count must change by one Gray step at a time, and with
  -- FCLK at most once per FCLK period, except while the design switches.
  p_gray_mon : process
    alias tg is << signal .tb_sp_pie_cpld.dut.t_gray : std_logic_vector(4 downto 0) >>;
    variable pv   : std_logic_vector(4 downto 0) := (others => '0');
    variable pt   : time := 0 ns;
    variable ones : natural;
    variable errs : natural := 0;
  begin
    wait on tg;
    ones := 0;
    for i in 0 to 4 loop
      if tg(i) /= pv(i) then
        ones := ones + 1;
      end if;
    end loop;
    if tm_ok = '1' and (ones /= 1 or
                        (tmode = 1 and now - pt < TF - 2 * FCLK_JIT * 1 ns)) then
      report "count step from " & to_string(pv) & " to " & to_string(tg) &
             " after " & time'image(now - pt) severity error;
      errs := errs + 1;
      gray_err <= errs;
    end if;
    pv := tg;
    pt := now;
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
      PI_SA0          => '0',
      PI_FCLK         => fclk,
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
  -- An empty pair must be exactly STL and C4..C0 in R0 and 0x08000 in R1.
  --
  -- Time checks. The reader notes the time of the SOE rising edge that ends
  -- each R1 read, which is the edge that loads the next R0.
  --   - An empty pair's C4..C0 must be the Gray code of a count in the
  --     window for that time (see window).
  --   - Two empty pairs in one run of reads: the step of C modulo 32 must be
  --     a step the two windows allow.
  --   - Each event is given the full count of the next empty pair (e) and
  --     the full count the Pi rebuilds from its T4..T0 (rec): the latest
  --     count at or below e whose Gray code is T4..T0. The main process
  --     compares these with the true count of the write.
  -- STL checks, between the load of the last empty pair (lp) and the load of
  -- this one (ln). Without FCLK, STL must be 0. With FCLK, STL must be 1
  -- when the design's stall pulse for a testbench stall T-state surely
  -- lies inside lp to ln, and may be 1 only when one can overlap it.
  -- The first pair of a run of reads was loaded by an edge the reader did
  -- not see (its own last read, a SYNC, or a read by the main process), so
  -- its count is not checked, and the STL of the empty pair after it is not
  -- checked either.
  ---------------------------------------------------------------------------
  p_reader : process
    variable lfsr   : unsigned(15 downto 0) := x"ACE1";
    variable r0, r1 : std_logic_vector(17 downto 0);
    variable errs   : natural := 0;
    variable emp    : natural := 0;
    variable timed  : natural := 0;
    variable pairs  : natural := 0;
    variable nstl   : natural := 0;
    variable nchk   : natural := 0;
    variable nmust  : natural := 0;

    -- Time of the SOE edge that loaded the R0 being read (ld_ok false when
    -- not known), and of the load of the last empty pair (pe_ok).
    variable ld_t     : time;
    variable ld_ok    : boolean := false;
    variable nx_t     : time;
    variable dummy_t  : time;
    variable pe_t     : time;
    variable pe_ok    : boolean := false;
    -- The last checked empty pair in this run: its C (binary) and window.
    variable pv_c     : integer := -1;
    variable pv_lo    : integer;
    variable pv_hi    : integer;

    type pend_t is array (0 to 63) of ev_t;
    variable pend   : pend_t;
    variable npend  : natural := 0;
    variable c, d, dc, lo, hi : integer;
    variable must, may : boolean;

    procedure step_lfsr is
    begin
      lfsr := lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
    end procedure;

    -- One SMI read. t is the time of the SOE rising edge that ends it.
    procedure smi_read(v : out std_logic_vector(17 downto 0); t : out time) is
    begin
      step_lfsr;
      soe_r <= '0';
      wait for 37 ns + (to_integer(lfsr(4 downto 0)) * 1 ns);
      v := sd17 & sd16 & sd;
      soe_r  <= '1';
      t      := now;
      step_lfsr;
      wait for 29 ns + (to_integer(lfsr(5 downto 0)) * 1 ns);
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

  begin
    loop
      if rd_en = '0' then
        flush(-1, 0);
        ld_ok := false;
        pe_ok := false;
        pv_c  := -1;
        rd_idle <= '1';
        wait until rd_en = '1';
      end if;
      rd_idle <= '0';
      smi_read(r0, dummy_t);
      smi_read(r1, nx_t);
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
        pend(npend) := (w => r1 & r0, lo => 0, hi => -1, nrm => false, e => -1, rec => -1);
        npend := npend + 1;
      elsif r0(17 downto 6) /= "000000000000" or r1 /= "00" & x"8000" then
        report "reader: empty pair not clean, R0=" & to_hstring(r0) & " R1=" & to_hstring(r1)
          severity error;
        errs := errs + 1;
      else
        emp := emp + 1;
        c   := bin5(r0(4 downto 0));
        if r0(5) = '1' then
          nstl := nstl + 1;
        end if;
        if ld_ok and tm_ok = '1' then
          timed := timed + 1;
          window(ld_t, tmode, toff, lo, hi);
          d := (c - lo) mod 32;
          if d > hi - lo then
            report "reader: empty pair count C=" & integer'image(c) & ", expected " &
                   integer'image(lo mod 32) & " to " & integer'image(hi mod 32) severity error;
            errs := errs + 1;
          end if;
          if pv_c >= 0 then
            dc := (c - pv_c) mod 32;
            if dc < lo - pv_hi or dc > hi - pv_lo then
              report "reader: empty pair count step " & integer'image(dc) &
                     ", expected " & integer'image(lo - pv_hi) & " to " &
                     integer'image(hi - pv_lo) severity error;
              errs := errs + 1;
            end if;
          end if;
          if pe_ok then
            nchk := nchk + 1;
            if tmode = 0 then
              must := false;
              may  := false;
            else
              must := h_stall.any_in(pe_t + EPS - PE_MIN, ld_t - EPS - PS_MAX);
              may  := h_stall.any_in(pe_t - EPS - PE_MAX, ld_t + EPS - PS_MIN);
            end if;
            if must then
              nmust := nmust + 1;
            end if;
            if r0(5) = '1' and not may then
              report "reader: STL = 1 without a stall" severity error;
              errs := errs + 1;
            elsif r0(5) = '0' and must then
              report "reader: STL = 0 after a stall" severity error;
              errs := errs + 1;
            end if;
          end if;
          -- The Pi's full count for this empty pair: the count in the
          -- window that C4..C0 names.
          flush(lo + d, c);
          pv_c  := c;
          pv_lo := lo;
          pv_hi := hi;
          pe_t  := ld_t;
          pe_ok := true;
        else
          flush(-1, 0);
          pv_c  := -1;
          pe_ok := false;
        end if;
      end if;
      ld_t  := nx_t;
      ld_ok := true;
      rd_errors  <= errs;
      rd_empties <= emp;
      rd_timed   <= timed;
      rd_pairs   <= pairs;
      rd_stl     <= nstl;
      rd_stlchk  <= nchk;
      rd_stlmust <= nmust;
    end loop;
  end process;

  ---------------------------------------------------------------------------
  -- Main test sequence.
  ---------------------------------------------------------------------------
  p_main : process
    -- /RESET after the design's filter. The reset event's count is the count
    -- at its rising edge.
    alias rst_n_dut is << signal .tb_sp_pie_cpld.dut.rst_n : std_logic >>;

    alias use_f_dut is << signal .tb_sp_pie_cpld.dut.use_f : std_logic >>;
    alias t_bin_dut is << signal .tb_sp_pie_cpld.dut.t_bin : unsigned(4 downto 0) >>;

    variable errors  : natural := 0;
    variable placed  : natural := 0;  -- events whose rebuilt count was checked
    variable inexact : natural := 0;  -- events whose window held two counts
    variable sn      : natural := 1;  -- suite number
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
    -- t is the time of the /WR rising edge.
    procedure mem_write(addr : natural; dat : natural; t : out time) is
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
      t := now;
    end procedure;

    procedure mem_write(addr : natural; dat : natural) is
      variable t : time;
    begin
      mem_write(addr, dat, t);
    end procedure;

    -- I/O write cycle (Zilog UM0080 figure 7): /IORQ and /WR fall after the
    -- rising edge of T2, one automatic wait state, both rise at the falling
    -- edge of T3. iorq_skew moves /IORQ against /WR (positive: /IORQ later).
    -- The data bus carries the inverted byte until T_IO_LATE after /WR falls.
    -- t is the time of the /WR rising edge.
    procedure io_write(addr : natural; dat : natural; iorq_skew : time;
                       t : out time) is
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
      t := now;
    end procedure;

    procedure io_write(addr : natural; dat : natural; iorq_skew : time := 0 ns) is
      variable t : time;
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

    -- An expected event. t is the time of the edge that ends it; nrm is true
    -- when the reader runs.
    procedure exp_ev(kind : natural; adr : natural; dat : natural;
                     ovf : std_logic; frm : std_logic; t : time) is
      variable lo, hi : integer;
    begin
      window(t, tmode, toff, lo, hi);
      exp.push((w => mk_ev(kind, adr, dat, ovf, frm, lo), lo => lo, hi => hi,
                nrm => rd_en = '1', e => -1, rec => -1));
    end procedure;

    procedure exp_mem(adr : natural; dat : natural; t : time;
                      ovf : std_logic := '0'; frm : std_logic := '0') is
    begin
      exp_ev(K_MEM, adr, dat, ovf, frm, t);
    end procedure;

    procedure exp_rst(t : time; ovf : std_logic := '0'; frm : std_logic := '0') is
    begin
      exp_ev(K_RESET, 0, 0, ovf, frm, t);
    end procedure;

    -- A memory write, and the event it must give.
    procedure mw(addr : natural; dat : natural; ovf : std_logic := '0';
                 frm : std_logic := '0') is
      variable t : time;
    begin
      mem_write(addr, dat, t);
      exp_mem(addr, dat, t, ovf, frm);
    end procedure;

    -- An I/O write, and the event it must give.
    procedure iw(addr : natural; dat : natural; skew : time := 0 ns;
                 ovf : std_logic := '0'; frm : std_logic := '0') is
      variable t : time;
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
    -- The fields must match exactly, and the time count T4..T0 must be in the
    -- write's window. For an event written while the reader runs, the next
    -- empty pair must come at most 31 counts after the write (its count e
    -- minus the write's count is 0 to 31), and the count the Pi rebuilds from
    -- T4..T0 must be the write's.
    procedure check(name : string) is
      variable g, x : ev_t;
      variable ok   : boolean := true;
      variable d, f : integer;
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
          d := (bin5(g.w(17 downto 16) & g.w(9) & g.w(35 downto 34)) - x.lo) mod 32;
          f := x.lo + d;
          if x.hi /= x.lo then
            inexact := inexact + 1;
          end if;
          if d > x.hi - x.lo then
            fail(name & ": event time count wrong, got " & to_hstring(g.w) &
                 ", expected " & to_hstring(x.w) & " (window " &
                 integer'image(x.hi - x.lo + 1) & ")");
            ok := false;
          elsif x.nrm and g.e >= 0 then
            placed := placed + 1;
            if g.e < f or g.e - f >= 32 then
              fail(name & ": next empty pair at count " & integer'image(g.e) &
                   ", event at " & integer'image(f));
              ok := false;
            elsif g.rec /= f then
              fail(name & ": Pi rebuilds count " & integer'image(g.rec) &
                   ", event at " & integer'image(f));
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
        report "ok   [" & integer'image(sn) & "] " & name;
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
        report "ok   [" & integer'image(sn) & "] " & name;
      end if;
    end procedure;

    -- A Spectrum reset. The address and data buses carry non-zero values
    -- while /RESET is low and as it goes high, so a reset event that took
    -- them from the bus would fail the address 0 and DATA 0 checks. t is the
    -- time when the filtered /RESET goes high, the edge that stores the
    -- reset event. /RESET stays low for eight Z80 clocks.
    procedure pulse_reset(t : out time) is
    begin
      wait until rising_edge(cpu_clk);
      reset_n <= '0';
      a       <= x"DA5A";
      d       <= x"A5";
      for i in 1 to 8 loop
        wait until rising_edge(cpu_clk);
      end loop;
      reset_n <= '1';
      wait until rst_n_dut = '1' for 60 * TCPU;
      if rst_n_dut /= '1' then
        fail("filtered /RESET did not go high");
      end if;
      t := now;
      wait for 4 * TCPU;
      a       <= (others => 'X');
      d       <= (others => 'Z');
    end procedure;

    procedure pulse_reset is
      variable t : time;
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
    variable tr     : time;

    -- Line the testbench count up with the design's count for mode m, at a
    -- moment when no count step is in the window.
    procedure calibrate(m : natural) is
      variable lo, hi : integer;
    begin
      loop
        window(now, m, 0, lo, hi);
        exit when lo = hi;
        wait for 7 ns;
      end loop;
      toff  <= (to_integer(t_bin_dut) - lo) mod 32;
      tmode <= m;
      wait for 0 ns;
    end procedure;

    -- Start FCLK (the Pi starts GPCLK0) or stop it, with the reader off. The
    -- design must switch its count; the testbench then lines its count up.
    procedure fclk_start is
    begin
      reader_off;
      tm_ok   <= '0';
      fclk_on <= '1';
      wait until use_f_dut = '1' for 20 us;
      if use_f_dut /= '1' then fail("FCLK started but not detected"); end if;
      wait for 1 us;
      calibrate(1);
      tm_ok <= '1';
      reader_on;
    end procedure;

    procedure fclk_stop is
    begin
      reader_off;
      tm_ok   <= '0';
      fclk_on <= '0';
      wait until use_f_dut = '0' for 100 us;
      if use_f_dut /= '0' then fail("FCLK stopped but still in use"); end if;
      wait for 1 us;
      calibrate(0);
      tm_ok <= '1';
      reader_on;
    end procedure;

    -- Invert the edge-connector clock, or stop inverting it. The change
    -- gives the clock one short or long phase, which no Spectrum does, so
    -- the reader is off and the testbench lines its count up again after it.
    procedure set_inv(v : std_logic) is
    begin
      reader_off;
      tm_ok   <= '0';
      clk_inv <= v;
      wait for 2 us;
      calibrate(tmode);
      tm_ok <= '1';
      reader_on;
    end procedure;

    -- The test suite after power-up. It runs once for each clock setup.
    procedure run_suite is
    begin
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
      set_inv('1');
      mw(16#4009#, 16#81#);
      iw(16#00FE#, 16#03#, 5 ns);
      iw(16#7FFD#, 16#07#);
      mw(16#C00A#, 16#82#);
      iw(16#1FFD#, 16#04#, -5 ns);
      check("inverted edge-connector clock");
      set_inv('0');

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
    end procedure;

  begin
    report "testbench DEPTH = " & integer'image(DEPTH);

    -- Power-up with /RESET low.
    wait for 3 us;
    reset_n <= '1';
    wait until rst_n_dut = '1';
    tr := now;
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

    run_suite;

    -- FCLK runs. Stalls in the high phase of the edge-connector clock, then
    -- in the low phase, then none.
    fclk_start;
    st_on  <= '1';
    st_lvl <= '1';
    sn := 2;
    run_suite;
    st_lvl <= '0';
    sn := 3;
    run_suite;
    st_on  <= '0';
    sn := 4;
    run_suite;

    -- FCLK stops: the count is the edge count again, stalls or not.
    st_on  <= '1';
    st_lvl <= '0';
    fclk_stop;
    sn := 5;
    run_suite;
    st_on  <= '0';

    -------------------------------------------------------------------------
    -- Results.
    -------------------------------------------------------------------------
    reader_off;
    if rd_errors /= 0 then
      fail("reader reported " & integer'image(rd_errors) & " bad read words");
    end if;
    if rd_timed = 0 then fail("no empty pair count was checked"); end if;
    if placed = 0 then fail("no event count was rebuilt from an empty pair"); end if;
    if gray_err /= 0 then
      fail(integer'image(gray_err) & " count steps were not one Gray step");
    end if;
    for k in 1 to 6 loop
      if st_k(k) = 0 then fail("no stalled phase of " & integer'image(k) & " T-states"); end if;
    end loop;
    if rd_stl = 0 then fail("no empty pair had STL = 1"); end if;
    if rd_stlmust = 0 then fail("no empty pair had to have STL = 1"); end if;
    report "reader: " & integer'image(rd_pairs) & " pairs, " &
           integer'image(rd_empties) & " empty, " & integer'image(rd_timed) &
           " empty counts checked, " & integer'image(placed) & " events placed, " &
           integer'image(inexact) & " events with a two-count window";
    report "stalls: k = 1 to 6: " & integer'image(st_k(1)) & " " &
           integer'image(st_k(2)) & " " & integer'image(st_k(3)) & " " &
           integer'image(st_k(4)) & " " & integer'image(st_k(5)) & " " &
           integer'image(st_k(6)) & "; STL = 1 on " & integer'image(rd_stl) &
           " empty pairs; STL checked on " & integer'image(rd_stlchk) &
           ", of which " & integer'image(rd_stlmust) & " had to be 1";

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
