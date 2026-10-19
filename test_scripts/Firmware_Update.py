"""Background firmware update operations for Device Monitor, without Tk calls."""
import importlib.util
from pathlib import Path
import threading
import time

_spec = importlib.util.spec_from_file_location(
    'viewer_firmware_client', Path(__file__).resolve().parents[1] / 'scripts/firmware_update.py')
fw = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fw)


## @brief Format an application slot identifier for display.
def slot_name(slot):
    return {0: 'A', 1: 'B', fw.NONE: 'None'}.get(slot, 'Unknown')


class UpdateWorker:
    """One bounded operation owns the update connection and publishes snapshots."""

    ## @brief Prepare an update operation and its thread-safe progress state.
    def __init__(self, mode, endpoint, port, action, *, image=None, version=0,
                 reset=True, receiver=None):
        self.mode, self.endpoint, self.port, self.action = mode, endpoint, port, action
        self.image, self.version, self.reset = image, version, reset
        self.receiver = receiver
        self.cancel = threading.Event()
        self.lock = threading.Lock()
        self.state = dict(message='Connecting to updater…', received=0, total=0,
                          report=None, outcome='working')
        self.thread = threading.Thread(target=self.run, daemon=True)

    ## @brief Copy the latest progress and device status for the Tk thread.
    def snapshot(self):
        with self.lock:
            return dict(self.state)

    ## @brief Publish progress changes without calling Tk from the update thread.
    def publish(self, **values):
        with self.lock:
            self.state.update(values)

    ## @brief Open the dedicated TCP update service or the exclusive USB endpoint.
    def connect(self, timeout=30):
        if self.mode == 'Ethernet':
            return fw.Client(tcp=self.endpoint, port=self.port, timeout=timeout)
        return fw.Client(usb=self.endpoint, timeout=timeout)

    ## @brief Publish acknowledged upload progress and the device's persistent flags.
    def progress(self, received, total, report):
        self.publish(received=received, total=total, report=report,
                     message='Verifying uploaded image…' if received == total else 'Uploading image…')

    ## @brief Query boot status until the candidate is confirmed, rolled back, or times out.
    def wait_for_boot(self, expected):
        self.publish(message='Reset requested; waiting for trial boot and confirmation…')
        deadline = time.monotonic() + 60
        # RESET is scheduled 500 ms after its acknowledgement. Avoid accepting
        # the old application's status before that reset has taken effect.
        while not self.cancel.wait(1) and time.monotonic() < deadline:
            client = None
            try:
                client = self.connect(timeout=2)
                report = client.request(fw.STATUS)
                self.publish(report=report)
                if report['pending'] == fw.NONE:
                    if report['running'] == expected == report['confirmed'] and report['flags'] & 8:
                        self.publish(outcome='confirmed', message=f'Update confirmed: slot {slot_name(expected)} is running.')
                        return
                    if report['running'] != expected:
                        self.publish(outcome='fallback', message=(
                            f'Fallback warning: slot {slot_name(expected)} did not remain active. '
                            f'The device is running slot {slot_name(report["running"])}.'))
                        return
                self.publish(message='Device is reachable; waiting for trial boot confirmation…')
            except (OSError, RuntimeError, ValueError):
                # Reset/re-enumeration and the watchdog's fallback window are
                # expected here. A reset is never sent a second time.
                pass
            finally:
                if client is not None:
                    client.close()
        self.publish(outcome='unconfirmed', message=(
            'Boot confirmation was not observed. Read status before another upload; '
            'for USB, check whether the serial port changed after reset.'))

    ## @brief Run an update or maintenance action while preserving exclusive USB ownership.
    def run(self):
        client = None
        try:
            if self.mode == 'USB' and self.receiver is not None:
                self.publish(message='Pausing USB streaming…')
                self.receiver.stop.set()
                self.receiver.thread.join(timeout=3)
                if self.receiver.thread.is_alive():
                    raise RuntimeError('USB stream has not closed yet; wait and retry.')
            if self.cancel.is_set():
                raise fw.UploadCancelled('Operation cancelled before connecting')
            client = self.connect()
            if self.cancel.is_set():
                raise fw.UploadCancelled('Operation cancelled before sending a request')
            if self.action == 'upload':
                report = fw.upload(client, self.image, version=self.version, reset=False,
                                   progress=self.progress, cancelled=self.cancel.is_set,
                                   log=lambda message: None)
                self.publish(report=report)
                if not self.reset or self.cancel.is_set():
                    self.publish(outcome='verified', message=(
                        f'Image verified in slot {slot_name(report["pending"])}. '
                        'Use Reset staged image to start its trial boot.'))
                    return
            else:
                operation = {'status': fw.STATUS, 'abort': fw.ABORT, 'clear': fw.CLEAR,
                             'reset': fw.STATUS}[self.action]
                report = client.request(operation)
                self.publish(report=report)
                if self.action != 'reset':
                    message = {'status': 'Device update status received.', 'abort': 'Incomplete upload aborted.',
                               'clear': 'Persistent warnings cleared.'}[self.action]
                    if report['flags'] & 16:
                        message += ' Fallback warning: the previous image was restored.'
                    self.publish(outcome='status', message=message)
                    return
            if self.cancel.is_set():
                raise fw.UploadCancelled('Operation cancelled before reset')
            expected = report['pending']
            if expected not in (0, 1):
                raise ValueError('There is no verified image staged for reset.')
            # Sending RESET once is the last mutation. Follow-up connections
            # only query status, including when an acknowledgement is lost.
            try:
                client.request(fw.RESET)
            except OSError:
                self.publish(message='Reset acknowledgement lost; checking boot status without resending reset…')
            client.close()
            client = None
            self.wait_for_boot(expected)
        except fw.UploadCancelled as error:
            self.publish(outcome='cancelled', message=str(error))
            if client is not None:
                try:
                    self.publish(report=client.request(fw.STATUS))
                except (OSError, RuntimeError, ValueError):
                    pass
        except (OSError, RuntimeError, ValueError, ImportError) as error:
            self.publish(outcome='error', message=(
                f'{error} Read device status before retrying; requests are not automatically replayed.'))
        finally:
            if client is not None:
                client.close()
