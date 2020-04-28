#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys

sys.path.append('../shared')
import gumpy
import numpy as np


"""
@input - EEG data (list); optional args
         
Method that applies different preprocessing techniques to the input data.

@output - Preprocessed data (list)
"""


def preprocess_data(data, sample_rate=160, ac_freq=60, hp_freq=0.5, bp_low=2, bp_high=60, notch=False,
                    hp_filter=False, bp_filter=False, artifact_removal=False, normalize=False):
    if notch:
        data = notch_filter(data, ac_freq, sample_rate)
    if hp_filter:
        data = highpass_filter(data, hp_freq)
    if bp_filter:
        data = bandpass_filter(data, bp_low, bp_high, sample_rate)
    if normalize:
        data = normalize_data(data, 'mean_std')
    if artifact_removal:
        data = remove_artifacts(data)

    return data


def notch_filter(data, ac_freq, sample_rate):
    w0 = ac_freq / (sample_rate / 2)
    return gumpy.signal.notch(data, w0)


def highpass_filter(data, hp_freq):
    return gumpy.signal.butter_highpass(data, hp_freq)


def bandpass_filter(data, bp_low, bp_high, sample_rate):
    return gumpy.signal.butter_bandpass(data, bp_low, bp_high, order=5, fs=sample_rate)


def normalize_data(data, strategy):
    return gumpy.signal.normalize(data, strategy)


def remove_artifacts(data):
    cleaned = gumpy.signal.artifact_removal(data.reshape((-1, 1)))[0]
    return np.squeeze(cleaned)
