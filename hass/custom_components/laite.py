"""
Support for RS485 boards made by Laite.

For more details about this component, please refer to the documentation at
https://hamclub.net/bg5hhp/home_smart/
"""
import logging

import voluptuous as vol

from homeassistant.const import (
    EVENT_HOMEASSISTANT_START, EVENT_HOMEASSISTANT_STOP)
from homeassistant.const import CONF_PORT
import homeassistant.helpers.config_validation as cv

REQUIREMENTS = ['pySerial==3.4']

_LOGGER = logging.getLogger(__name__)

BOARD = None

DOMAIN = 'laite'

CONFIG_SCHEMA = vol.Schema({
    DOMAIN: vol.Schema({
        vol.Required(CONF_PORT): cv.string,
    }),
}, extra=vol.ALLOW_EXTRA)


def setup(hass, config):
    """Set up the Laite component."""
    import serial

    port = config[DOMAIN][CONF_PORT]

    global BOARD
    try:
        BOARD = LaiteBoard(port)
    except (serial.serialutil.SerialException, FileNotFoundError):
        _LOGGER.error("Your port %s is not accessible", port)
        return False

    # try:
    #     BOARD.get_switch_states("01 02 03 04")
    # except serial.serialutil.SerialException:
    #     _LOGGER.warning("Error reading board status")
    #     return False

    def stop_laite(event):
        """Stop the Laite service."""
        BOARD.disconnect()

    def start_laite(event):
        """Start the Laite service."""
        hass.bus.listen_once(EVENT_HOMEASSISTANT_STOP, stop_laite)

    hass.bus.listen_once(EVENT_HOMEASSISTANT_START, start_laite)

    return True


class LaiteBoard(object):
    """Representation of an Laite X160 board."""

    def __init__(self, port):
        """Initialize the board."""
        import serial,threading
        self._port = port
        self._board = serial.Serial(port=self._port, baudrate=9600, timeout=3)
        self.lock = threading.Lock()

    def set_mode(self, pin, direction, mode):
        """Set the mode of the board input."""
        pass

    def write_command(self, cmd_str):
        """ write the hex string of command to the 485 bus """
        _LOGGER.debug("==> write command: %s", cmd_str)
        _data = bytearray.fromhex(cmd_str)
        _len = self._board.write(_data)
        _LOGGER.debug("%d bytes send", _len)

    def set_switch_on(self, addr, sw_idx):
        """ Set the switch line on, sw_idx == 16 to switch all on """
        if(sw_idx >= 0 and sw_idx < 16):
            # turn one switch on
            sw_idx_str = " %02x " % (sw_idx,)
            cmd = "1A " + addr + " 00" + sw_idx_str + "01" + " 0F"
            self.write_command(cmd)
            return True #should check the status by async querying status
        elif sw_idx == 0xff:
            # turn all light on if idx = 16
            cmd = "1A " + addr + " 30 77 88" + " 0F"
            self.write_command(cmd)
            return True
        else:
            # invalid switch index!
            _LOGGER.error("Invalid switch index [%s]", sw_idx)
            return False

    def set_switch_off(self, addr, sw_idx):
        """ Set the switch line off, sw_idx == 0 to switch all on """
        if(sw_idx >= 0 and sw_idx < 16):
            # turn one switch off
            sw_idx_str = " %02x " % (sw_idx,)
            cmd = "1A " + addr + " 00" + sw_idx_str + "00" + " 0F"
            self.write_command(cmd)
            return True
        elif sw_idx == 0xff:
            # turn all light on if idx = 16
            cmd = "1A " + addr + " 40 77 88" + " 0F"
            self.write_command(cmd)
            return True
        else:
            # invalid switch index!
            _LOGGER.error("Invalid switch index [%s]", sw_idx)
            return False

    def get_switch_states_unsafe(self, addr, sw_idx=16):
        """ Get the switch status """
        if(sw_idx < 0 or sw_idx > 16):
            _LOGGER.error("Invalid switch index [%s]", sw_idx)
            return
        # send the query command
        self._board.flushInput()
        cmd = "1A " + addr + " 86 86 86" + " 0F"
        self.write_command(cmd)
        # read the response
        data = self._board.read((5 + 16 + 1))
        _LOGGER.debug("<== read data: %s",data)
        if len(data) != 16:
            _LOGGER.warning("read board status failed, response %d bytes", len(data))
            return None
        status = data[5:16]
        _LOGGER.debug("switch status: %s", status)
        return status

    def get_switch_states(self, addr, sw_idx=16):
        """ Get the switch status, synchronized """
        with self.lock:
            return self.get_switch_states_unsafe(addr, sw_idx)

    # def get_analog_inputs(self):
    #     """Get the values from the pins."""
    #     pass
    #     # self._board.capability_query()
    #     # return self._board.get_analog_response_table()

    # def set_digital_out_high(self, pin):
    #     """Set a given digital pin to high."""
    #     pass
    #     # self._board.digital_write(pin, 1)

    # def set_digital_out_low(self, pin):
    #     """Set a given digital pin to low."""
    #     pass
    #     # self._board.digital_write(pin, 0)

    # def get_digital_in(self, pin):
    #     """Get the value from a given digital pin."""
    #     pass
    #     # self._board.digital_read(pin)

    # def get_analog_in(self, pin):
    #     """Get the value from a given analog pin."""
    #     pass
    #     # self._board.analog_read(pin)

    # def get_firmata(self):
    #     """Return the version of the Firmata firmware."""
    #     pass
    #     # return self._board.get_firmata_version()

    def disconnect(self):
        """Disconnect the board and close the serial connection."""
        self._board.close()
