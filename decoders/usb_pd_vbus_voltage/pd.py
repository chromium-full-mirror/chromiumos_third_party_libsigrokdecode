##
## This file is part of the libsigrokdecode project.
##
## Copyright (C) 2014 Google, Inc
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##

import sigrokdecode as srd
import ctypes

class Decoder(srd.Decoder):
    api_version = 2
    id = 'usb_pd_vbus_voltage'
    name = 'pd_vbus_voltage'
    longname = 'VBus voltage decoder for vbus'
    desc = 'VBus voltage decoder for vbus'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = ['vbus_value']
    channels = (
        {'id': 'vbus', 'name': 'VBus Voltage', 'desc': 'VBus voltage value'},
    )
    options = (
        {'id':'voltage_threshold', 'desc':'The threshold to determine changes', 'default':100},
    )

    annotations = (
       ('value', 'usb pd vbus voltage value'),
       ('bits', 'encoding bits for the vbus value'),
       ('warnings', 'Human-readable warning'),
    )

    annotation_rows = (
       ('title', 'VBus Voltage', (0,)),
       ('warnings', 'Warnings', (2,)),
    )

    def __init__(self, **kwargs):
        self.samplerate = None
        self.samplenum = 0
        self.vbus = 0
        self.previous_vbus = 0
        self.previous = []
        self.offset = 0
        self.timestamp = 0

    def metadata(self, key, value):
        if key == srd.SRD_CONF_SAMPLERATE:
            self.samplerate = value

    def start(self):
        self.out_python = self.register(srd.OUTPUT_PYTHON)
        self.out_ann = self.register(srd.OUTPUT_ANN)
        self.out_binary = self.register(srd.OUTPUT_BINARY)
        self.out_bitrate = self.register(srd.OUTPUT_META,
                meta=(int, 'Bitrate', 'Bitrate during the packet'))

    def putx(self, data):
        self.put(self.samplenum - 33, self.samplenum, self.out_ann, data)

    def get_vbus(self):
        vbus_value = 0
        for i in range(1, 17):
            vbus_value |= (self.previous[i] << (i-1))
        return vbus_value

    def get_offset(self):
        offset_value = 0
        for i in range(17, 33):
            offset_value |= (self.previous[i] << (i-17))
        return ctypes.c_short(offset_value).value

    def decode(self, ss, es, data):
        if self.samplerate is None:
            raise Exception("Cannot decode without samplerate.")
        for (self.samplenum, pins) in data:
            if len(self.previous) == 0:
                if pins[0]==0: # mustn't be the start of a vbus value, so ignore it
                    continue
                else:
                    self.previous.append(pins[0])
            elif len(self.previous) == 33: # may be end of a vbus value + offset
				# current time stamp in ms
                self.tstamp = float(self.samplenum - 33) / self.samplerate * 1000
                self.vbus = self.get_vbus()
                self.offset = self.get_offset()
                if (self.vbus - self.previous_vbus < - self.options['voltage_threshold'] or
                    self.vbus - self.previous_vbus > self.options['voltage_threshold']):
                    self.putx([0, ['Volt: %5dmV, Tstamp: %7.3fms, Offset: %3dms'
                              %(self.vbus, self.tstamp, self.offset)]])
                    self.previous_vbus = self.vbus
                self.previous = []
                if pins[0]==1:
                    self.previous.append(pins[0])
            else: # add the bit to the packet
                self.previous.append(pins[0])
