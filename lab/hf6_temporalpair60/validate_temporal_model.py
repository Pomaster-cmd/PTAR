from collections import deque
import math

PERIOD = 1000.0 / 60.0
SLOTS = 3

class TemporalModel:
    def __init__(self):
        self.ready = deque()
        self.current = None
        self.phase = 'G'
        self.next_deadline = None
        self.last_submit = None
        self.events = []
        self.drops = 0
        self.starved = True

    def publish(self, seq, t):
        # Three pair slots total. One may be DISPLAYING and up to two READY.
        occupied = len(self.ready) + (1 if self.current is not None else 0)
        if occupied >= SLOTS:
            if self.ready:
                self.ready.popleft()
                self.drops += 1
            else:
                # All capacity is displaying/writing in the real code: source
                # drops this pair rather than ever blocking or bursting.
                self.drops += 1
                return
        self.ready.append((seq, t))

    def _claim(self, now):
        if self.current is not None or not self.ready:
            return
        self.current = self.ready.popleft()
        self.phase = 'G'
        if self.next_deadline is None:
            self.next_deadline = now + PERIOD
        elif self.starved:
            if self.last_submit is not None and now - self.last_submit < PERIOD:
                self.next_deadline = self.last_submit + PERIOD
            else:
                self.next_deadline = now
        self.starved = False

    def advance_to(self, limit):
        eps = 1e-9
        while True:
            self._claim(limit if self.next_deadline is None else min(limit, self.next_deadline))
            if self.current is None:
                self.starved = True
                return
            if self.next_deadline is None or self.next_deadline > limit + eps:
                return
            seq, _ = self.current
            t = self.next_deadline
            self.events.append((t, seq, self.phase))
            self.last_submit = t
            self.next_deadline += PERIOD
            if self.phase == 'G':
                self.phase = 'R'
            else:
                self.current = None
                self.phase = 'G'
                # If no pair is ready at this exact point, enter starvation.
                if not self.ready:
                    self.starved = True
                    return


def run(arrivals, tail_ms=500.0):
    m = TemporalModel()
    last = 0.0
    for seq, t in enumerate(arrivals, 1):
        m.advance_to(t)
        m.publish(seq, t)
        # Claim immediately after publication when starved; the grid decides
        # whether this is a safe immediate recovery or a one-slot wait.
        m._claim(t)
        last = t
    m.advance_to(last + tail_ms)
    return m


def analyze(name, model, expect_full60):
    ev = model.events
    assert len(ev) >= 20, (name, len(ev))
    # Strict G/R within every displayed pair.
    by_seq = {}
    for t, seq, typ in ev:
        by_seq.setdefault(seq, []).append((t, typ))
    complete = [v for v in by_seq.values() if len(v) == 2]
    assert complete, name
    for v in complete:
        assert v[0][1] == 'G' and v[1][1] == 'R', (name, v)
        assert abs((v[1][0] - v[0][0]) - PERIOD) < 1e-6, (name, v)

    # The central temporal invariant: never recover with a micro-burst.
    intervals = [ev[i][0] - ev[i-1][0] for i in range(1, len(ev))]
    assert min(intervals) >= PERIOD - 1e-6, (name, min(intervals))

    # Ignore warm-up/tail and evaluate a stable central window.
    body = ev[8:-8] if len(ev) > 24 else ev
    if len(body) > 2:
        dur = body[-1][0] - body[0][0]
        fps = (len(body)-1) * 1000.0 / dur
    else:
        fps = 0.0

    g_to_r = []
    r_to_g = []
    for i in range(1, len(body)):
        dt = body[i][0] - body[i-1][0]
        if body[i-1][2] == 'G' and body[i][2] == 'R':
            g_to_r.append(dt)
        elif body[i-1][2] == 'R' and body[i][2] == 'G':
            r_to_g.append(dt)
    assert g_to_r and r_to_g, name
    med = lambda xs: sorted(xs)[len(xs)//2]
    gmed, rmed = med(g_to_r), med(r_to_g)
    assert abs(gmed - PERIOD) < 1e-6, (name, gmed)
    if expect_full60:
        assert abs(rmed - PERIOD) < 1e-6, (name, rmed)
        assert fps >= 59.9, (name, fps)
    else:
        # Starvation may create 33ms+ R->G gaps, but never a sub-slot burst.
        assert rmed >= PERIOD - 1e-6, (name, rmed)

    print(f'{name}: events={len(ev)} fps={fps:.3f} Gmed={gmed:.3f} Rmed={rmed:.3f} min={min(intervals):.3f} drops={model.drops} PASS')


def constant_pairs(ms, count=240):
    return [i * ms for i in range(count)]

# Sustained >=30 pair/s sources: must become an exact 60-Hz G/R stream.
for ms in (20.0, 26.0, 30.0, 32.0, 33.0):
    analyze(f'CONST_{ms:.1f}MS', run(constant_pairs(ms)), True)

# Jittered source whose long-term rate remains >30 pair/s. The 3-pair mailbox
# must absorb phase jitter without ever generating a G/R micro-burst.
pattern = [27.0, 31.0, 29.0, 35.0, 30.0, 32.0, 28.0, 34.0]
t = 0.0
arr=[]
for i in range(320):
    arr.append(t)
    t += pattern[i % len(pattern)]
analyze('JITTER_MEAN30.75MS', run(arr), True)

# Field-like under-production (~28.8 pairs/s): 60 forever is mathematically
# impossible. The required behavior is graceful starvation with NO catch-up
# microbursts; this directly targets the 0.5/31ms defect.
analyze('UNDERPRODUCED_34.715MS', run(constant_pairs(34.715, 260)), False)

# Explicit starvation/recovery: after a 120ms hole, recovery must never put two
# submissions closer than one 60-Hz period.
arr = constant_pairs(30.0, 80)
base = arr[-1] + 120.0
arr += [base + i*30.0 for i in range(100)]
analyze('STARVATION_120MS_RECOVERY', run(arr), False)

print('TEMPORAL_MODEL_STRICT_G_R=PASS')
print('TEMPORAL_MODEL_NO_MICROBURST=PASS')
print('TEMPORAL_MODEL_SUSTAINED_30PAIR_TO_60FPS=PASS')
print('TEMPORAL_MODEL_STARVATION_NO_CATCHUP=PASS')
