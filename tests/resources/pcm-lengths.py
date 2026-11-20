import ctypes as c

lib = c.CDLL("libasound.so.2")


def call(name, types, *args):
    f = getattr(lib, name)
    f.argtypes = types
    f.restype = c.c_int
    result = f(*args)
    if result < 0:
        raise RuntimeError(f"{name} {result}")
    return result


P, I, U = c.c_void_p, c.c_int, c.c_uint
handle = P()
call("snd_pcm_open", [c.POINTER(P), c.c_char_p, I, I], c.byref(handle), b"hw:0,0", 0, 0)
call("snd_pcm_set_params", [P, I, I, U, U, I, U], handle, 2, 3, 2, 48000, 0, 180000)
for length in [12001, 32768, 48016]:
    buf = (c.c_short * (2 * length))()
    n = call("snd_pcm_writei", [P, P, c.c_ulong], handle, buf, length)
    assert n == length
    call("snd_pcm_drain", [P], handle)
    call("snd_pcm_prepare", [P], handle)
    print("drained", length, "frames", flush=True)
call("snd_pcm_close", [P], handle)
