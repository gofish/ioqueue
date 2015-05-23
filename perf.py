import os
import sys

def test(requests, bufsize, depth, binary, path):
    cmd = 'REQUESTS=%d BUFSIZE=%d Q_DEPTH=%d %s %s'
    cmd %= (requests, bufsize, depth, binary, path)
    ret = os.system(cmd)
    if ret:
        raise SystemExit(ret)

def testmt(requests, bufsize, depth, binary, path):
    test(requests, bufsize, depth, binary + 'mt', path)

args = list(sys.argv[1:])
binary = args.pop(0)
path = args.pop(0)
depth = int(args.pop(0)) if args else 32
for s in xrange(8):
    requests = 1 << (18 - s/3)
    bufsize = 1 << (9 + s)
    test(requests, bufsize, depth, binary, path)
