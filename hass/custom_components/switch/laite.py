"""
Support for switching Laite pins on and off.

So far only digital pins are supported.

For more details about this platform, please refer to the documentation at
https://hamclub.net/bg5hhp/home_smart/
"""
import logging

import voluptuous as vol

import custom_components.laite as laite
from homeassistant.components.switch import (SwitchDevice, PLATFORM_SCHEMA)
from homeassistant.const import (CONF_NAME, CONF_ADDRESS)
import homeassistant.helpers.config_validation as cv

DEPENDENCIES = ['laite']

_LOGGER = logging.getLogger(__name__)

CONF_CHANNELS = 'channels'
#CONF_NEGATE = 'negate'
#CONF_INITIAL = 'initial'

DEVICE_SCHEMA = vol.Schema({
    vol.Required(CONF_NAME): cv.string,
    #vol.Optional(CONF_INITIAL, default=False): cv.boolean,
    #vol.Optional(CONF_NEGATE, default=False): cv.boolean,
})

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
    vol.Required(CONF_ADDRESS): cv.string,
    vol.Required(CONF_CHANNELS, default={}):
        vol.Schema({cv.positive_int: DEVICE_SCHEMA}),
})


def setup_platform(hass, config, add_devices, discovery_info=None):
    """Set up the Laite platform."""
    # Verify that Laite board is present
    if laite.BOARD is None:
        _LOGGER.error("A connection has not been made to the Laite board")
        return False

    address = config.get(CONF_ADDRESS)
    channels = config.get(CONF_CHANNELS)
    switches = []

    _LOGGER.debug("Setup Laite X160 Switches [addr=%s]", address)
    _states = laite.BOARD.get_switch_states(address) # get all switch status
    for sw_idx, sw_opt in channels.items():
        sw_state = 0
        if _states is not None and len(_states) == 16:
            sw_state = _states[sw_idx]
        switches.append(LaiteSwitch(address, sw_idx, sw_state, sw_opt))
    add_devices(switches)


class LaiteSwitch(SwitchDevice):
    """Representation of an Laite X160 switch."""

    def __init__(self, sw_addr, sw_idx, sw_state, sw_opt):
        """Initialize the Pin."""
        self._addr = sw_addr
        self._idx = sw_idx
        self._name = sw_opt.get(CONF_NAME)

        # read the switch state
        # self._state = sw_opt.get(CONF_INITIAL)
        if sw_state > 0:
            self._state = True
        else:
            self._state = False

        # TODO - restore the switch state if possible

        # if options.get(CONF_NEGATE):
        #     self.turn_on_handler = laite.BOARD.set_digital_out_low
        #     self.turn_off_handler = laite.BOARD.set_digital_out_high
        # else:
        #     self.turn_on_handler = laite.BOARD.set_digital_out_high
        #     self.turn_off_handler = laite.BOARD.set_digital_out_low

        # laite.BOARD.set_mode(self._pin, self.direction, self.pin_type)
        # (self.turn_on_handler if self._state else self.turn_off_handler)(pin)
        self.turn_on_handler = laite.BOARD.set_switch_on
        self.turn_off_handler = laite.BOARD.set_switch_off

        _LOGGER.debug("Switch[%s_%d] \"(%s)\" loaded", sw_addr, sw_idx, self._name)

    @property
    def name(self):
        """Get the name of the switch."""
        return self._name

    @property
    def is_on(self):
        """Return true if switch is on."""
        return self._state

    def turn_on(self):
        """Turn the switch on."""
        self._state = self.turn_on_handler(self._addr, self._idx)

    def turn_off(self):
        """Turn the switch off."""
        self._state = (not self.turn_off_handler(self._addr, self._idx))
