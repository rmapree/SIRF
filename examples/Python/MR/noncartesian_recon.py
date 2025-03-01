'''
Example of radial phase encoding (RPE) reconstruction.

Upper-level demo that illustrates the computation of how to use a non-cartesian
radial phase-encoding acquisition model to reconstruct data. The backward method
ignores any non-uniform density in k-space and the inverse does account for it
by using a ramp kernel as in a filtered backprojection.

Usage:
  rpe_recon.py [--help | options]

Options:
  -f <file>, --file=<file>    raw data file
                              [default: 3D_RPE_Lowres.h5]
  -p <path>, --path=<path>    path to data files, defaults to data/examples/MR
                              subfolder of SIRF root folder
  -o <file>, --output=<file>  output file for simulated data
  -e <engn>, --engine=<engn>  reconstruction engine [default: Gadgetron]
  --traj=<str>                trajectory type, must match the data supplied in file
                              options are cartesian, radial, goldenangle or grpe
                              [default: grpe]
  --recon                     reconstruct iff non-cartesian code was compiled
  --non-interactive           do not show plots
'''

## SyneRBI Synergistic Image Reconstruction Framework (SIRF)
## Copyright 2021 Physikalisch-Technische Bundesanstalt (PTB)
## Copyright 2015 - 2021 Rutherford Appleton Laboratory STFC
## Copyright 2015 - 2021 University College London.
##
## This is software developed for the Collaborative Computational
## Project in Synergistic Reconstruction for Biomedical Imaging (formerly CCP PETMR)
## (http://www.ccpsynerbi.ac.uk/).
##
## Licensed under the Apache License, Version 2.0 (the "License");
##   you may not use this file except in compliance with the License.
##   You may obtain a copy of the License at
##       http://www.apache.org/licenses/LICENSE-2.0
##   Unless required by applicable law or agreed to in writing, software
##   distributed under the License is distributed on an "AS IS" BASIS,
##   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
##   See the License for the specific language governing permissions and
##   limitations under the License.

__version__ = '0.1.0'
from docopt import docopt
args = docopt(__doc__, version=__version__)

#from pUtilities import *

import numpy as np
import matplotlib.pyplot as plt

from sirf.Utilities import error, examples_data_path, existing_filepath, show_3D_array

# import engine module
import importlib
mr = importlib.import_module('sirf.' + args['--engine'])

# process command-line options
data_file = args['--file']
data_path = args['--path']
if data_path is None:
    data_path = examples_data_path('MR') + '/zenodo/'
output_file = args['--output']
show_plot = not args['--non-interactive']
trajtype = args['--traj']
run_recon = args['--recon']

def main():

    # locate the k-space raw data file adn read
    input_file = existing_filepath(data_path, data_file)
    acq_data = mr.AcquisitionData(input_file)
    
    # pre-process acquisition data
    if trajtype != 'radial' and trajtype != 'goldenangle':
        print('---\n pre-processing acquisition data...')
        processed_data  = mr.preprocess_acquisition_data(acq_data)
    else:
        processed_data = acq_data

    #set the trajectory and compute the dcf
    print('---\n setting the trajectory...')
    if trajtype == 'cartesian':
        pass
    elif trajtype == 'grpe':
        processed_data = mr.set_grpe_trajectory(processed_data)
    elif trajtype == 'radial':
        processed_data = mr.set_radial2D_trajectory(processed_data)
    elif trajtype == 'goldenangle':
            processed_data = mr.set_goldenangle2D_trajectory(processed_data)
    else:
        raise NameError('Please submit a trajectory name of the following list: (cartesian, grpe, radial). You gave {}'\
                        .format(trajtype))

    if show_plot:
        traj = np.transpose( mr.get_data_trajectory(processed_data))
        print("--- traj shape is {}".format(traj.shape))
        plt.figure()
        plt.scatter(traj[0,:], traj[1,:], marker='.')
        plt.show()

    # sort processed acquisition data;
    print('---\n sorting acquisition data...')
    processed_data.sort()

    if run_recon is True:
    
        print('---\n computing coil sensitivity maps...')
        csms = mr.CoilSensitivityData()
        csms.smoothness = 10
        csms.calculate(processed_data)

        if show_plot:
        # display coil sensitivity maps
            csms_array = csms.as_array()
            nz = csms_array.shape[1]
            title = 'SRSS from raw data (magnitude)'
            show_3D_array(abs(csms_array[:, nz//2, :, :]), suptitle=title, \
                    xlabel='samples', ylabel='readouts', label='coil', show=False)
        
        # create acquisition model based on the acquisition parameters
        print('---\n Setting up Acquisition Model...')
    
        acq_model = mr.AcquisitionModel()
        acq_model.set_up(processed_data, csms.copy())
        acq_model.set_coil_sensitivity_maps(csms)
    
        print('---\n Backward projection ...')
        #recon_img = acq_model.backward(processed_data)
        bwd_img = acq_model.backward(processed_data)
        inv_img = acq_model.inverse(processed_data)
        
        if show_plot:
            bwd_img.show(title = 'Reconstructed images using backward() (magnitude)')
            inv_img.show(title = 'Reconstructed images using inverse() (magnitude)')
    
    else:
        print('---\n Skipping non-cartesian code...')

try:
    main()
    print('\n=== done with %s' % __file__)

except error as err:
    # display error information
    print('??? %s' % err.value)
    exit(1)

