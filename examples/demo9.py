import math
import pylab
import sys
import time

sys.path.append('../../build/xGadgetron')
sys.path.append('../pGadgetron')

from pGadgetron import *

try:
    # acquisitions will be read from this HDF file
    input_data = ISMRMRDAcquisitions('testdata.h5')

    print('---\n acquisition data norm: %e' % input_data.norm())

    interim_data = MR_remove_x_oversampling(input_data)

    print('---\n processed acquisition data norm: %e' % interim_data.norm())

    # perform reconstruction
    recon = SimpleReconstructionProcessor()
    recon.set_input(interim_data)
    recon.process()
    interim_images = recon.get_output()

    print('---\n reconstructed images norm: %e' % interim_images.norm())

    csms = MRCoilSensitivityMaps()

    # coil sensitivity maps can be read from a file
##    csm_file = str(input('csm file: '))
##    print('reading sensitivity maps...')
##    csms.read(csm_file)
    # or computed
    print('ordering acquisitions...')
    input_data.order()
    print('computing sensitivity maps...')
    csms.compute(input_data)

    # create acquisition model based on the acquisition parameters
    # stored in input_data and image parameters stored in interim_images
    am = AcquisitionModel(input_data, interim_images)

    am.set_coil_sensitivity_maps(csms)

    # use the acquisition model (forward projection) to produce acquisitions
    acqs = am.forward(interim_images)

    print('---\n their forward projection norm %e' % acqs.norm())

    # compute the difference between real and modelled acquisitions:
    #   diff = acqs - P acqs,
    # where P is the orthogonal projector onto input_data
    a = -acqs.dot(input_data) / input_data.dot(input_data)
    b = 1.0
    diff = AcquisitionsContainer.axpby(a, input_data, b, acqs)
    rr = diff.norm()/acqs.norm()
    print('---\n reconstruction residual norm (rel): %e' % rr)

    # apply the adjoint model (backward projection)
    imgs = am.backward(diff)

    print('---\n its backward projection norm: %e' % imgs.norm())

except error as err:
    # display error information
    print ('Gadgetron exception occured:\n', err.value)
