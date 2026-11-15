import json, math, sys, time, urllib.request

API = 'http://127.0.0.1:8777/api/state'


def state():
    with urllib.request.urlopen(API, timeout=8) as r:
        return json.load(r)


def metrics(conv, width, height):
    cx, cy = width / 2, height / 2
    self_s = conv.get('self')
    if not self_s:
        return None
    off = math.hypot(self_s[0] - cx, self_s[1] - cy)
    ring = conv.get('ring') or []
    if len(ring) < 20:
        return (off, None, None, 0, self_s)
    xs = [p[0] for p in ring]
    ys = [p[1] for p in ring]
    bw, bh = max(xs) - min(xs), max(ys) - min(ys)
    circ = abs(bw - bh) / max(bw, bh, 1.0)
    on = sum(1 for p in ring if 0 <= p[0] < width and 0 <= p[1] < height)
    return (off, circ, (bw + bh) / 4, on, self_s)


for i in range(6):
    d = state()
    width, height = d.get('client') or [0, 0]
    if i == 0:
        print('client', d.get('client'), 'heroes', len(d.get('heroes') or []),
              'range', d.get('range'), 'team', d.get('team'))
        for h in d.get('heroes') or []:
            print('  hero', h['index'], h['team'], repr(h['name']), h.get('position'))
        for p in d.get('positions') or []:
            print('  pos', hex(p['offset']), 'chain', p.get('chain'), p.get('value'))
    print('\nsample %d focused=%s crc=%s' % (i, d.get('focused'), d.get('viewCrc')))
    for conv in d.get('conventions') or []:
        m = metrics(conv, width, height)
        if not m:
            print('  %-18s self=off-screen' % conv['name'])
            continue
        off, circ, rad, on, self_s = m
        print('  %-18s self=(%6.0f,%6.0f) offCentre=%7.0f circ=%s rad=%s on=%s' % (
            conv['name'], self_s[0], self_s[1], off,
            'n/a' if circ is None else '%.3f' % circ,
            'n/a' if rad is None else '%.1f' % rad, on))
    time.sleep(0.8)
