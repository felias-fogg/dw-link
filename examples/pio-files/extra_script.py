
Import("env", "projenv")
import subprocess

def dw_stop(*args, **kwargs):
    print("Disable debugWire")
    tc = env.Dump().find('toolchain-atmelavr')
    sl = env.Dump().find('/',tc+1)
    if (sl > tc and sl >= 0 and tc >= 0):
        toolchain = env.Dump()[tc:sl]
    else:
        toolchain = "toolchain-atmelavr-original"
    gdb = [env['PROJECT_PACKAGES_DIR'] + '/' + toolchain + '/bin/avr-gdb' ]
    gdb += ["-batch-silent", "-b", "115200", "-ex", "target remote " + \
            env['UPLOAD_PORT'], "-ex", "monitor dwoff"]
    try:
        ret = subprocess.call(gdb, shell=False)
        print("DWEN fuse disabled")
    except subprocess.CalledProcessError as exc:
        print("Call failed with " + str(exc))



env.AddCustomTarget(
    name="dwstop",
    dependencies=None,
    actions=[
        dw_stop
    ],
    title="DebugWire Stop",
    description="Stops debugWire mode and re-enables the reset line"
)

