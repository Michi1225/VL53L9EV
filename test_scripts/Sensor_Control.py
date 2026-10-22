"""Magnecko Sense control protocol helpers (USB CDC and streaming TCP connection)."""
from USB_Debug import HEADER, TOF_MAGIC, TOF_STATUS, TOF_STATUS_V2

CTRL_MAGIC = b'CTRL'
GET_CONFIG, START, STOP, SET_CONFIG, RESET, POWER_DOWN = 1, 2, 3, 4, 5, 6
STATE_NAMES = ('Initializing', 'Stopped', 'Streaming', 'Fault — reset required', 'Powered down')
POWER_NAMES = ('Regular', 'Low', 'Ultra low')
CONTEXT_NAMES = ('Short', 'Long')
RANGING_MODES = ('Precision (no DSS)', 'Precision', 'Ambient')

## @brief Translate the selected ranging mode into sensor power and context settings.
def ranging_mode(context, dss_enabled):
    return ('Ambient' if context else 'Precision') if dss_enabled else 'Precision (no DSS)'
RESULT_NAMES = ('OK', 'Unknown command', 'Invalid configuration', 'Device busy',
                'Sensor operation failed', 'Stop ToF before applying settings',
                'Sensor fault is latched; use Reset ToF', 'Sensor is powered down; use Reset ToF')
OPERATION_NAMES = {GET_CONFIG: 'Read configuration', START: 'Start ToF', STOP: 'Stop ToF',
                   SET_CONFIG: 'Apply configuration', RESET: 'Reset ToF', POWER_DOWN: 'Power down ToF'}


## @brief Check host-side ToF settings before encoding a sensor command.
def validate_config(config):
    fields = ('period_us', 'exposure_ms', 'binning', 'power', 'context')
    if not isinstance(config, dict) or set(config) not in (set(fields), set(fields) | {'dss_enabled'}) or any(type(config[key]) is not int for key in fields):
        raise ValueError('Configuration fields must be integers')
    if type(config.get('dss_enabled', True)) is not bool:
        raise ValueError('DSS setting must be a boolean')
    if config['context'] == 1 and not config.get('dss_enabled', True):
        raise ValueError('Precision (no DSS) requires the short context')
    if not 10000 <= config['period_us'] <= 1000000:
        raise ValueError('Frame period must be 10–1000 ms')
    if not 1 <= config['exposure_ms'] <= 30:
        raise ValueError('Exposure must be 1–30 ms')
    if config['exposure_ms'] * 1000 > config['period_us']:
        raise ValueError('Exposure must not exceed the frame period')
    if config['binning'] not in (2, 4, 6, 8, 12, 24):
        raise ValueError('Unsupported binning')
    if config['power'] not in range(3) or config['context'] not in range(2):
        raise ValueError('Unsupported power mode or range context')


## @brief Encode a versioned CTRL request with the validated configuration when required.
def build_command(request_id, operation, config=None):
    import struct
    if not 1 <= request_id <= 0xffffffff or operation not in OPERATION_NAMES:
        raise ValueError('Invalid command or request ID')
    payload = b''
    if operation == SET_CONFIG:
        validate_config(config)
        payload = struct.pack('<IHHBBH', config['period_us'], config['exposure_ms'],
                              config['binning'], config['power'], config['context'], int(not config.get('dss_enabled', True)))
    elif config is not None:
        raise ValueError('Only SET_CONFIG accepts a payload')
    return HEADER.pack(CTRL_MAGIC, request_id, len(payload), 2 if operation == SET_CONFIG else 1, operation) + payload


## @brief Decode and validate a versioned TOFS acknowledgement or status packet.
def decode_status(packet):
    layout = {1: TOF_STATUS, 2: TOF_STATUS_V2}.get(packet.width)
    if packet.kind != TOF_MAGIC or layout is None or len(packet.payload) != layout.size:
        raise ValueError('Unsupported sensor status packet')
    values = layout.unpack(packet.payload)
    timestamp, revision, error, period, exposure, binning, power, context, state, result, width, height, frames = values[:13]
    flags, reserved = values[13:] if packet.width == 2 else (0, 0)
    if flags & ~1 or reserved:
        raise ValueError('Invalid sensor status flags')
    if state >= len(STATE_NAMES) or result >= len(RESULT_NAMES) or power >= 3 or context >= 2:
        raise ValueError('Invalid sensor status')
    config = dict(period_us=period, exposure_ms=exposure, binning=binning, power=power, context=context, dss_enabled=not bool(flags & 1))
    if state in (1, 2):
        validate_config(config)
        if width != 100 or height != {2: 149, 4: 39, 6: 18, 8: 9, 12: 6, 24: 3}[binning]:
            raise ValueError('Invalid CSI dimensions')
    return dict(request_id=packet.sequence, operation=packet.height, timestamp_ms=timestamp,
                revision=revision, error=error, config=config, state=state, result=result,
                width=width, height=height, frames=frames)
