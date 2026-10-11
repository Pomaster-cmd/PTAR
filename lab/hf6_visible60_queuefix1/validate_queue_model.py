from math import isclose

PERIOD=1000.0/60.0
CAPACITY=2  # D3D9 BackBufferCount=2: front + two queued backbuffers

def run(work_ms, pairs=240):
    t=0.0
    next_vblank=PERIOD
    queue=[]
    shown=[]

    def consume_until(limit, inclusive=False):
        nonlocal next_vblank
        eps=1e-9
        while next_vblank < limit-eps or (inclusive and next_vblank <= limit+eps):
            if queue:
                shown.append((next_vblank,queue.pop(0)))
            next_vblank += PERIOD

    for seq in range(1,pairs+1):
        target=t+work_ms
        consume_until(target, inclusive=True)
        t=target
        for typ in ('G','R'):
            while len(queue)>=CAPACITY:
                if next_vblank<t:
                    consume_until(t,inclusive=True)
                    continue
                t=next_vblank
                if queue:
                    shown.append((next_vblank,queue.pop(0)))
                next_vblank += PERIOD
            queue.append((seq,typ))

    while queue:
        if next_vblank<t:
            consume_until(t,inclusive=True)
            continue
        shown.append((next_vblank,queue.pop(0)))
        next_vblank+=PERIOD

    return shown

def check_sustainable(work_ms):
    shown=run(work_ms)
    # Ignore first 8 visible outputs while the queue fills.
    body=shown[8:-8]
    assert body, work_ms
    for i,(t,(seq,typ)) in enumerate(body):
        if i:
            dt=t-body[i-1][0]
            assert abs(dt-PERIOD)<1e-6,(work_ms,dt)
        if i:
            prev=body[i-1][1][1]
            assert prev!=typ,(work_ms,prev,typ)
    duration=body[-1][0]-body[0][0]
    fps=(len(body)-1)*1000.0/duration
    assert abs(fps-60.0)<1e-6,(work_ms,fps)
    print(f'WORK_{work_ms:.1f}MS_VISIBLE_FPS={fps:.3f} PASS')

for work in (20.0,26.0,30.0,32.5):
    check_sustainable(work)

# Explicitly prove we do not claim impossible 60 when pair production itself
# is slower than 30 Hz. At 34 ms/pair the model must show a missed vblank.
slow=run(34.0)
dts=[slow[i][0]-slow[i-1][0] for i in range(1,len(slow))]
assert any(dt>PERIOD*1.5 for dt in dts), 'source-limit guard failed'
print('SOURCE_LIMIT_34MS_MISSED_VBLANK=PASS')
print('VISIBLE60_QUEUE_MODEL=PASS')
