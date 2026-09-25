"""Run ESP-IDF's long linker-generator command without Windows cmd.exe."""
import subprocess
import sys

Import("env")

if sys.platform == "win32":
    original_verbose_action = env.VerboseAction

    def verbose_action(self, action, message):
        if isinstance(action, str) and "ldgen.py" in action:
            def run_ldgen(target, source, env):
                command = env.subst(action, target=target, source=source)
                # CreateProcess accepts longer commands than cmd.exe's 8191 limit.
                return subprocess.call(
                    command,
                    shell=False,
                    env={str(key): str(value) for key, value in env["ENV"].items()},
                )

            return original_verbose_action(run_ldgen, message)
        return original_verbose_action(action, message)

    env.AddMethod(verbose_action, "VerboseAction")
