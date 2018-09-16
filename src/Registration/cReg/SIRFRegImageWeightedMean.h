/*
CCP PETMR Synergistic Image Reconstruction Framework (SIRF)
Copyright 2015 - 2017 Rutherford Appleton Laboratory STFC

This is software developed for the Collaborative Computational
Project in Positron Emission Tomography and Magnetic Resonance imaging
(http://www.ccppetmr.ac.uk/).

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

/*!
\file
\ingroup Registration
\brief Perform weighted mean of multiple images.

\author Richard Brown
\author CCP PETMR
*/

#ifndef _SIRFREGIMAGEWEIGHTEDMEAN_H_
#define _SIRFREGIMAGEWEIGHTEDMEAN_H_

#include <nifti1_io.h>
#include <string>
#include <vector>
#include "SIRFImageData.h"
#include "SIRFImageDataDeformation.h"

namespace sirf {
/// Calculate the weighted mean of a set of images
template<class ImType>
class SIRFRegImageWeightedMean
{
public:

    /// Constructor
    SIRFRegImageWeightedMean();

    /// Destructor
    ~SIRFRegImageWeightedMean() {}

    /// Add an image (from filename) and its corresponding weight
    void add_image(const std::string &filename, const float weight);

    /// Add an image (from SIRFImageData) and its corresponding weight
    void add_image(const ImType &image, const float weight);

    /// Update
    void update();

    /// Save image to file
    void save_image_to_file(const std::string &filename) const;

    /// Get output
    const ImType &get_output() const { return _output_image; }

protected:

    /// Check if its possible to calculate the mean
    void check_can_do_mean() const;

    /// Bool to check if update is necessary
    bool                      _need_to_update;
    /// Vector of input images
    std::vector<ImType> _input_images;
    /// Vector of weights
    std::vector<float>        _weights;
    /// Output image
    ImType                    _output_image;

};

/// Typedef for 3D
typedef SIRFRegImageWeightedMean<SIRFImageData>            SIRFRegImageWeightedMean3D;
/// Typedef for 4D
typedef SIRFRegImageWeightedMean<SIRFImageDataDeformation> SIRFRegImageWeightedMean4D;
}

#endif
