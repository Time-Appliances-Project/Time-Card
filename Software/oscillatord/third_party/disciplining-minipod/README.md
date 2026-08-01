# Disciplining Minipod

C disciplining algorithm used with **oscillatord** to discipline oscillators using the [minipod algorithm](https://bitbucket.org/spectracom/minipod/src/master/) developed by Matthias Lorentz.

## Algorithm principle

The goal of the algorithm is to minimize the phase error between the 1PPS signal generated from the 10MHz output of the oscillator and the reference 1PPS signal (generally coming from a GNSS receiver). In nominal conditions this phase alignment is performed by careful steering of the oscillator frequency based on the calibration of the frequency offset control. A phase jump can also be triggered if needed in case of a too-large initial phase offset value.

The algorithm is currently only working with Orolia miniature Rubidium oscillator mRO-50, but can be adapted to any frequency-controllable oscillator.

The mRO-50 oscillator's frequency adjustment has two modes:
  - **Coarse alignment**: rough adjustment at digital PLL level
  - **Fine alignment**: precise adjustment at C-field level

For more details on these frequency adjustment modes, please refer to the manual available at https://www.orolia.com/product/spectratime-mro-50/

At each call the algorithm is supplied with the following values:
  - Frequency adjustment values applied (coarse and fine values for mRO-50)
  - Phase error between the reference 1PPS and the oscillator 1PPS
  - Oscillator validity flag (lock status)
  - GNSS reference validity flag

During normal operation the coarse adjustment value is left untouched. After a calibration process (see section below), the mRO-50 is set to a coarse alignment value when fine alignment can optimally minimize the phase error. The algorithm is called in a loop and at each call the fine alignment value needed to minimize the phase error is evaluated and applied.


## Algorithm calibration for mRO-50 oscillator

The calibration process allows Minipod to compute, at a particular coarse alignment value, the correspondence between frequency error and fine adjustment values. This correspondence is based on linear regression of a sample of phase error values obtained at a given number of configurable points in the range of fine alignment value.

Consistency check of the calibration parameters consists in the verification that null frequency error can be reached within the available range. If so, the calibration is finished and the parameters are saved.
If not, this means that either the external conditions of the calibration were not right or the current coarse alignment of the mRO-50 is wrong. The algorithm will redo the calibration process after modifying the applied coarse value according to the estimated offset.

:warning: In order to preserve the mRO-50 from loosing its lock, the coarse alignment shift is limited hence the whole calibration could take several steps before finding the optimal coarse alignment in case of large initial frequency misalignment.

:warning: Calibration must be triggered only when the good quality of the reference can be guaranteed. Any flaw at the reference level would translated into a flawed calibration law.

## Compilation

### Build

```bash
mkdir build && cd build
cmake ..
make
```

### Install locally

``` bash
cd build
sudo make install

```

## Tests

Some tests have been implemented in the test directory:

- **test-utils**: Tests the utils files (linear regression and linear interpolation)
- **test-minipod**:
  - Test od_new_from_config
  - Test od_get_calibration parameters

### Build tests

Test utils:

``` bash
cd test
make test-utils
./test-utils
```

Test minipod:

``` bash
make test-minipod
./test-minipod
```

## Parameters

- `ctrl_node_length`: Number of control points for the linear regression
- `ctrl_load_nodes`: Array of **ctrl_node_length** float values between 0 and 1 (100%) as a percentage of available control. This is translated into fine alignment values (0 -> minimum fine value , 1 -> maximum fine value)
- `ctrl_drift_coeffs`: Array of **ctrl_node_length** float values corresponding the coefficients of the control law computed at calibration step (or during manual calibration) from the linear interpolation at each `ctrl_load_nodes`. They correspond the the estimated relative frequency error at each node.
- `coarse_equilibrium`: optimal coarse value corresponding minimal coarse frequency error.
- `reactivity_min`, `reactivity_max` and `reactivity_power`: Reactivity is a parameter controlling the aggressiveness of the algorithm for phase error minimization. Lower reactivity values means faster convergence but non-optimal frequency stability
  - reactivity_min should not go below a few settling times (SETTLING_TIME in code).
  - reactivity_max should ideally correspond to the timescale of optimal oscillator stability.
  - Decrease power for more peaked profile (more reactive).

- `nb_calibration`: Number of phase error samples to get for each control points when doing a calibration.
- `fine_stop_tolerance`: Tolerance authorized for estimated equilibrium in algorithm calibration.
- `max_allowed_coarse`: Maximum coarse shift that can be requested by the algorithm during calibration.
- `phase_jump_threshold_ns`: phase error threshold (in ns) beyond which a phase jump is triggered

## License

Disciplining Minipod is under GNU Lesser General Public License (LGPL).
