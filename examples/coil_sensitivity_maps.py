'''
Medium-level demo demonstrating how 2D coil sensitivity maps can be obtained 
from a multi-coil 2D Cartesian MR acquisition
'''

import argparse
import math
import matplotlib.pyplot as plt
import os
import sys
import time

BUILD_PATH = os.environ.get('BUILD_PATH') + '/xGadgetron'
SRC_PATH = os.environ.get('SRC_PATH') + '/xGadgetron/pGadgetron'

sys.path.append(BUILD_PATH)
sys.path.append(SRC_PATH)

from pGadgetron import *
from ismrmrdtools import coils

parser = argparse.ArgumentParser(description = \
'''
Medium-level demo demonstrating how 2D coil sensitivity maps can be obtained 
from a multi-coil 2D Cartesian MR acquisition
''')
parser.add_argument\
('filename', nargs='?', default = 'simulated_MR_2D_cartesian.h5', \
 help = 'raw data file name (default: simulated_MR_2D_cartesian.h5)')
args = parser.parse_args()

def show(image_matrix, tile_shape, scale, titles):
    assert numpy.prod(tile_shape) >= image_matrix.shape[0],\
            "image tile rows x columns must equal the 3rd dim"\
            " extent of image_matrix"
    cols, rows = tile_shape
    vmin, vmax = scale
    fig = plt.figure()
    for z in range(image_matrix.shape[0]):
        ax = fig.add_subplot(cols, rows, z+1)
        ax.set_title(titles[z])
        ax.set_axis_off()
        imgplot = ax.imshow(image_matrix[z,:,:], vmin=vmin, vmax=vmax)
    print('close figure 1 to continue')
    plt.show()


def main():
    # acquisitions will be read from an HDF file args.filename
    input_data = AcquisitionData(args.filename)
    
    # pre-process acquired k-space data
    processed_data = MR_remove_x_oversampling(input_data)
    
    # sort k-space data into a 2D Cartesian matrix for each coil
    processed_data.sort()
    
    # create object containing images for each coil
    CIs = CoilImages()
    CIs.calculate(processed_data)

    # create coil sensitivity object
    CSMs = CoilSensitivityMaps()

    # calculate coils using 
    CSMs.calculate(CIs, method = 'SRSS(niter = 10)')


    # display coil sensitivity maps
    coil_images = numpy.squeeze(CSMs.as_array(0))
    maxv = numpy.amax(coil_images)
    show(abs(coil_images[0::2,:,:]), tile_shape = (1,4), scale = (0, maxv),\
        titles = ['Abs(Coil1)', 'Abs(Coil3)','Abs(Coil5)','Abs(Coil7)'])
    show(numpy.angle(coil_images[0::2,:,:]), tile_shape = (1,4), scale = (0, maxv),\
        titles = ['Angle(Coil1)', 'Angle(Coil3)','Angle(Coil5)','Angle(Coil7)'])    
    
try:
    main()
    print('done')

except error as err:
    # display error information
    print('??? %s' % err.value)
