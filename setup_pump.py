from distutils.core import setup,Extension

setup(name="Pump", version="1.0",
    ext_modules=[
       Extension( "_Pump", ["pump.c" ],
       include_dirs = ["./sbc"],
       library_dirs = ["/usr/lib/arm-linux-gnueabinf", "./sbc"],
       libraries = ["rt", "sbc"] )
    ]
)
