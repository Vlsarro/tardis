from tardis import model
import numpy as np
import pytest


"""
def f_nu_to_f_lambda(self, f_nu):
        return f_nu * self.frequency.value**2 / constants.c.cgs.value / 1e8
"""


"""
def calculate_updated_radiationfield(self, nubar_estimator, j_estimator):
    updated_t_rads = t_rad_estimator_constant * nubar_estimator / j_estimator
    updated_ws = j_estimator / (
        4 * constants.sigma_sb.cgs.value * updated_t_rads ** 4 * self.time_of_simulation.value
        * self.tardis_config.structure.volumes.value)

    return updated_t_rads * u.K, updated_ws
"""

def test_f_nu_to_f_lambda():
    # here will be some code
    pass


def test_calculate_updated_radiationfield():
    # expecting another piece of code
    pass
