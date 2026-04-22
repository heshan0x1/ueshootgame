# Copyright Epic Games, Inc. All Rights Reserved.

#-------------------------------------------------------------------------------
class Printer(object):
    def __del__(self):
        print("\x1b]9;4;0;0\x07", end="")

    def _thread(self):
        try:
            for line in self._stdout:
                self._print(line.decode(errors="replace").rstrip())
        except (IOError, ValueError):
            pass
        finally:
            self._stdout.close()

    def run(self, runnable):
        if not hasattr(self, "_print"):
            name = type(self).__name__
            raise SyntaxError("Printer '{name}' must implement a _print() method")

        self._stdout = runnable.launch(stdout=True)

        import threading
        thread = threading.Thread(target=self._thread, daemon=True)
        thread.start()
        runnable.wait()

    def set_progress(self, percent):
        percent = int(max(0, min(percent, 100)))
        print(f"\x1b]9;4;1;{percent}\x07", end="")
