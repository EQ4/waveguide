TODO
====

* work plan

* raytracer + waveguide alignment

* sort out boundary conditions
* work out how to do mic modelling or hrtf (multichannel)

* look into the Helmholtz equation

* enveloping
    * look into better envelope models
    * starting with better attenuation coefficients for the current method

* look into sample rate conversion
    * think about ring direction (shouldn't ring both ways!)
        * github.com/AlexHarker/M4L_Convolution_Reverb_Externals/bufresample.c
    * test libsamplerate

* interpolate initial impulse position

CRITICAL
========

* generate 10 to 20 example outputs
    * different waveguide spacings
    * different filter frequencies
    * different models
    * mono/stereo
    * different input position interpolation methods

MAYBE
=====

* remove outside nodes
* precompute neighbor nodes on the GPU instead
    * *and benchmark*!

How it works
============

* provide a maximum estimation frequency
* calculate sampling frequency, which is directly related to valid frequency
  range
    * (factor of 1/4)
* calculate grid spacing, which is also related to sampling frequency
    * and the speed of sound in the modelling medium (air)
* build a mesh of nodes
    * find axis-aligned bounding-box for entire model
    * fill it with nodes with appropriate spacing
    * for each node, calculate whether it is inside or outside the model
        * requires models which are completely closed
    * optional - precompute neighbor nodes - probably faster as a one-off
      action rather than every step in the simulation
        * currently doing this on the CPU, could do it on the GPU
* designate an input and an output node
* for each time step
    * add the input signal to the current pressure value at the input node
    * for each neighbor node to the current node
        * add their current pressure
    * divide by two
    * subtract the previous pressure at the current node
    * multiply by attenuation factor
    * if this is the output node, write out the current pressure value
