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

# 600Khz datarate
UI_US = 1000000/600000.0

# Threshold to discriminate half-1 from 0
THRESHOLD_US = (UI_US + 2 * UI_US) / 2


class Decoder(srd.Decoder):
    api_version = 2
    id = 'usb_pd_bmc'
    name = 'pd_bmc'
    longname = 'Biphase Mark Decoder for USB Power Delivery baseband protocol'
    desc = 'Power Delivery protocol over CC wire on USB type-C'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = ['pd_packet']
    channels = (
        {'id': 'cc', 'name': 'CC', 'desc': 'Control channel'},
    )
    options = (
    )
    annotations = (
       ('packet', 'USB-PD packet'),
       ('bits', 'decoded BMC'),
       ('warnings', 'Human-readable warning'),
    )
    annotation_rows = (
       ('bits', 'Bits', (1,)),
       ('title', 'packet', (0,)),
       ('warnings', 'Warnings', (2,)),
    )

    def us2samples(self, us):
        if self.samplerate is None:
            raise Exception("Need the samplerate.")
        return int(us * self.samplerate / 1000000)

    def __init__(self, **kwargs):
        self.samplerate = None
        self.samplenum = 0
        self.seq = 0
        self.previous = 0
        self.oldpins = [0]
        self.startsample = None
        self.packet = []
        self.edges = []
        self.half_one = False
        self.start_one = 0
        self.tstamp = 0

    def metadata(self, key, value):
        if key == srd.SRD_CONF_SAMPLERATE:
            self.samplerate = value
            # 0 is 2 UI, space larger than 1.5x 0 is definitely wrong
            self.maxbit = self.us2samples(3 * UI_US)
            # duration threshold between half 1 and 0
            self.threshold = self.us2samples(THRESHOLD_US)

    def start(self):
        self.out_python = self.register(srd.OUTPUT_PYTHON)
        self.out_ann = self.register(srd.OUTPUT_ANN)
        self.out_binary = self.register(srd.OUTPUT_BINARY)
        self.out_bitrate = self.register(srd.OUTPUT_META,
                meta=(int, 'Bitrate', 'Bitrate during the packet'))

    def putbitx(self, start, data):
        self.put(start, self.samplenum, self.out_ann, data)

    def putx(self, data):
        self.put(self.startsample, self.previous, self.out_ann, data)

    def putp(self, data):
        self.put(self.startsample, self.previous, self.out_python, data)

    def putb(self, data):
        self.put(self.startsample, self.previous, self.out_binary, data)

    def decode(self, ss, es, data):
        if self.samplerate is None:
            raise Exception("Cannot decode without samplerate.")
        for (self.samplenum, pins) in data:
            # find edges ...
            if self.oldpins == pins:
                continue

            self.oldpins, (cc,) = pins, pins

            # First sample of the packet, just record the start date
            if not self.startsample:
                self.startsample = self.samplenum
                self.previous = self.samplenum
                continue

            diff = self.samplenum - self.previous

            # Large idle : use it as the end of packet
            if diff > self.maxbit:
                tstamp = float(self.startsample) / self.samplerate
                #brate float(self.previous - self.start) / len(self.packet) / self.samplerate if len(self.packet) else 0
                # the last edge of the packet
                self.edges.append(self.previous)
                # Export the packet
                self.putx([0, ['BMC %d' % (self.seq),'PD']])
                self.putp({'TIMESTAMP':tstamp, 'BITS': self.packet, 'EDGES':self.edges})
                #self.putb((1, bytes(self.packet))
                # Reset for next packet
                self.startsample = self.samplenum
                self.packet = []
                self.edges = []
                self.half_one = False
                self.start_one = 0
                self.seq += 1
            else: # add the bit to the packet
                is_zero = diff > self.threshold
                if is_zero and not self.half_one:
                    self.packet.append(0)
                    self.putbitx(self.previous,[1,['0','0']])
                    self.edges.append(self.previous)
                elif not is_zero and self.half_one:
                    self.packet.append(1)
                    self.putbitx(self.start_one,[1,['1','1']])
                    self.edges.append(self.start_one)
                    self.half_one = False
                elif not is_zero and not self.half_one:
                    self.half_one = True
                    self.start_one = self.previous
                else: # Invalid BMC sequence
                    self.putbitx(self.start_one,[2,['invalid duration %2.2fus' % (float(diff)*1000000/self.samplerate),'%d' % (diff)]])
                    self.putbitx(self.start_one,[1,['X','X']])
                    #TODO try to recover
                    self.packet.append(0)
                    self.edges.append(self.previous)
                    self.half_one = False
            self.previous = self.samplenum
