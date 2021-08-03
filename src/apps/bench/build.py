dirs = [
    'accelchain',
    'bench-apps',
    'compute',
    'cppbenchs',
    'cppnetbenchs',
    'fs',
    'fstrace',
    'imgproc',
    'ipc',
    'loadgen',
    'rustbenchs',
    'rustnetbenchs',
    'scale',
    'scale-pipe',
    'tlbmiss',
    'vpe-clone',
    'vpe-exec',
    'ycsb',
]

def build(gen, env):
    for d in dirs:
        env.sub_build(gen, d)
