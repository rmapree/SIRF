/*
SyneRBI Synergistic Image Reconstruction Framework (SIRF)
Copyright 2015 - 2023 Rutherford Appleton Laboratory STFC
Copyright 2018 - 2023 University College London
Copyright 2020 - 2023 Physikalisch-Technische Bundesanstalt (PTB)

This is software developed for the Collaborative Computational
Project in Synergistic Reconstruction for Biomedical Imaging (formerly CCP PETMR)
(http://www.ccpsynerbi.ac.uk/).

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
\ingroup MR
\brief Implementation file for SIRF data container classes for Gadgetron data.

\author Evgueni Ovtchinnikov
\author Johannes Mayer
\author SyneRBI
*/
#include <algorithm> 
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <ismrmrd/ismrmrd.h>
#include <ismrmrd/version.h>
#include <ismrmrd/xml.h>

#include "sirf/common/iequals.h"
#include "sirf/iUtilities/LocalisedException.h"
#include "sirf/Gadgetron/cgadgetron_shared_ptr.h"
#include "sirf/Gadgetron/gadgetron_data_containers.h"
#include "sirf/Gadgetron/gadgetron_x.h"

using namespace gadgetron;
using namespace sirf;

shared_ptr<MRAcquisitionData> MRAcquisitionData::acqs_templ_;

static std::string get_date_time_string()
{
    time_t rawtime;
    struct tm * timeinfo;
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );

    std::stringstream str;
    str << timeinfo->tm_year+1900 << "-"
        << std::setw(2) << std::setfill('0') << timeinfo->tm_mon+1 << "-"
        << std::setw(2) << std::setfill('0') << timeinfo->tm_mday << " "
        << std::setw(2) << std::setfill('0') << timeinfo->tm_hour << ":"
        << std::setw(2) << std::setfill('0') << timeinfo->tm_min << ":"
        << std::setw(2) << std::setfill('0') << timeinfo->tm_sec;
    return str.str();
}

void 
MRAcquisitionData::write(const std::string &filename) const
{
	Mutex mtx;
    std::ifstream file;
    mtx.lock();
    file.open(filename.c_str());
    if (file.good()) {
        file.close();
        int err = std::remove(filename.c_str());
        if (err)
            std::cerr << "deleting " << filename.c_str() << " failed, appending...\n";
    }
    file.close();
	shared_ptr<ISMRMRD::Dataset> dataset
		(new ISMRMRD::Dataset(filename.c_str(), "/dataset", true));
	dataset->writeHeader(acqs_info_.c_str());
	mtx.unlock();
	int n = number();
	ISMRMRD::Acquisition a;
	for (int i = 0; i < n; i++) {
		get_acquisition(i, a);
		mtx.lock();
		dataset->appendAcquisition(a);
		mtx.unlock();
	}
}

void
MRAcquisitionData::read(const std::string& filename_ismrmrd_with_ext, int all)
{
	
    bool const verbose = true;

	if( verbose )
		std::cout<< "Started reading acquisitions from " << filename_ismrmrd_with_ext << std::endl;
	try
	{
		Mutex mtx;
		mtx.lock();
		ISMRMRD::Dataset d(filename_ismrmrd_with_ext.c_str(),"dataset", false);
		d.readHeader(this->acqs_info_);

        ISMRMRD::IsmrmrdHeader hdr = acqs_info_.get_IsmrmrdHeader();
        
        uint32_t num_acquis = d.getNumberOfAcquisitions();
		mtx.unlock();

		std::stringstream str;
		std::string xml = this->acqs_info_.c_str();
		size_t i = xml.find("<version>");
		if (i != std::string::npos) {
			size_t j = xml.find("</version>");
			int va = std::stoi(xml.substr(i + 9, j - i - 9));
			int v = ISMRMRD_XMLHDR_VERSION;
			if (va > v) {
				str << "Input acquisition file was written in with "
				<< "ISMRMRD XML version "<< va 
				<< ", but the version of ISMRMRD used presently by SIRF "
				<< "supports XML version " << v 
				<< " or less only, terminating...";
				THROW(str.str());
			}
			else if (va < v) {
				std::cout << "WARNING: ";
				std::cout << "acquisitions header version (" << va;
				std::cout << ") is older than ISMRMRD header version (" << v;
				std::cout << "), ignoring...\n";
				this->acqs_info_ = xml.substr(0, i) + xml.substr(j + 10);
			}
		}

		for( uint32_t i_acqu=0; i_acqu<num_acquis; i_acqu++)
		{
			if( verbose )
			{
				if( i_acqu%( num_acquis/10 ) == 0 )
					std::cout << std::ceil(float(i_acqu) / num_acquis * 100) << "%.." << std::flush;
			}

			ISMRMRD::Acquisition acq;
			mtx.lock();
			d.readAcquisition( i_acqu, acq);
			mtx.unlock();

			if(all || !TO_BE_IGNORED(acq))
				this->append_acquisition( acq );
		}
        this->sort_by_time();
		if( verbose )
			std::cout<< "\nFinished reading acquisitions from " << filename_ismrmrd_with_ext << std::endl;
	}
	catch( std::runtime_error& e)
	{
		std::cerr << "An exception was caught reading " << filename_ismrmrd_with_ext << std::endl;
		std::cerr << e.what() <<std::endl;
		throw;
	}
}

bool
MRAcquisitionData::undersampled() const
{
	ISMRMRD::IsmrmrdHeader header = acqs_info_.get_IsmrmrdHeader();
	ISMRMRD::Encoding e = header.encoding[0];
	return e.parallelImaging.is_present() &&
		e.parallelImaging().accelerationFactor.kspace_encoding_step_1 > 1;
}

int 
MRAcquisitionData::get_acquisitions_dimensions(size_t ptr_dim) const
{
    int na = number();
    ASSERT(na > 0, "You are asking for dimensions on an empty acquisition container. Please don't...");

    int* dim = (int*)ptr_dim;

    ISMRMRD::Acquisition acq;
    int ns = 0;
    int nc = 0;
    int num_acq = 0;
    for (int i = 0; i < na; ++i)
    {
        if (!get_acquisition(i, acq))
            continue;
        if (num_acq == 0) {
            ns = acq.number_of_samples();
            nc = acq.active_channels();
        }
        else {
            ASSERT(acq.number_of_samples() == ns, "One of your acquisitions has a different number of samples. Please make sure the dimensions are consistent.");
            ASSERT(acq.active_channels() == nc, "One of your acquisitions has a different number of active channels. Please make sure the dimensions are consistent.");
        }
        num_acq++;
    }

    int const num_dims = 3;
    dim[0] = ns;
    dim[1] = nc;
    dim[2] = num_acq;

    return num_dims;
}

uint16_t MRAcquisitionData::get_trajectory_dimensions(void) const
{
    int na = number();
    ASSERT(na > 0, "You are asking for dimensions on an empty acquisition container. Please don't...");

    ISMRMRD::Acquisition acq;
    uint16_t traj_dims = 65535;
    for (int i = 0; i < na; ++i)
    {
        if (!get_acquisition(i, acq))
            continue;
        if (traj_dims == 65535)
            traj_dims = acq.trajectory_dimensions();
        else if (acq.trajectory_dimensions() != traj_dims)
            throw LocalisedException("Not every acquisition in your container has the same trajectory dimension." , __FILE__, __LINE__);
    }
    return traj_dims;
}

void MRAcquisitionData::get_kspace_dimensions(std::vector<size_t>& dims) const
{
    int na = number();
    ASSERT(na > 0, "You are asking for dimensions on an empty acquisition container. Please don't...");

    ISMRMRD::Acquisition acq;
    int nro = -1;
    int nc;
    for (int i = 0; i < na; ++i)
    {
        if (!get_acquisition(i, acq))
            continue;
        if (nro == -1) {
            nro = acq.number_of_samples();
            nc = acq.active_channels();
        }
        else {
            if (acq.active_channels() != nc)
                throw std::runtime_error("The number of channels is not consistent within this container.");
            if (acq.number_of_samples() != nro)
                throw std::runtime_error("The number of readout points is not consistent within this container.");
        }
    }

    ISMRMRD::IsmrmrdHeader hdr = this->acquisitions_info().get_IsmrmrdHeader();
    ISMRMRD::Encoding e = hdr.encoding[0];
    ISMRMRD::EncodingSpace enc_space = e.encodedSpace;

    std::vector<size_t> empty_data;
    dims.swap(empty_data);
    dims.push_back(nro);
    dims.push_back(enc_space.matrixSize.y);
    dims.push_back(enc_space.matrixSize.z);
    dims.push_back(nc);
}



void
MRAcquisitionData::get_data(complex_float_t* z, int a)
{
	ISMRMRD::Acquisition acq;
	unsigned int na = number();
	if (a >= 0 && a < na) {
		get_acquisition(a, acq);
		unsigned int nc = acq.active_channels();
		unsigned int ns = acq.number_of_samples();
		for (unsigned int c = 0, i = 0; c < nc; c++) {
			for (unsigned int s = 0; s < ns; s++, i++) {
				z[i] = acq.data(s, c);
			}
		}
		return;
	}
	for (unsigned int a = 0, i = 0; a < na; a++) {
		if (!get_acquisition(a, acq)) {
			std::cout << "ignoring acquisition " << a << '\n';
			continue;
		}
		unsigned int nc = acq.active_channels();
		unsigned int ns = acq.number_of_samples();
		for (unsigned int c = 0; c < nc; c++) {
			for (unsigned int s = 0; s < ns; s++, i++) {
				z[i] = acq.data(s, c);
			}
		}
	}
}

void
MRAcquisitionData::set_user_floats(float const * const z, int const idx)
{

    if(idx >= ISMRMRD::ISMRMRD_USER_FLOATS)
        throw LocalisedException("You try to set the user floats of an index higher than available in the memory of ISMRMRDAcquisition. Pass a smaller idx." , __FILE__, __LINE__);

    ISMRMRD::Acquisition acq;
    for(int ia=0;ia<this->number();ia++)
    {
        this->get_acquisition(ia, acq);
        acq.user_float()[idx] = *(z+ia);
        this->set_acquisition(ia,acq);
    }
}

void 
MRAcquisitionData::axpby
(complex_float_t a, const ISMRMRD::Acquisition& acq_x,
	complex_float_t b, ISMRMRD::Acquisition& acq_y)
{
	const complex_float_t* px;
	complex_float_t* py;
	for (px = acq_x.data_begin(), py = acq_y.data_begin();
		px != acq_x.data_end() && py != acq_y.data_end(); px++, py++) {
		if (b == complex_float_t(0.0))
			*py = a * (*px);
		else
			*py = a * (*px) + b * (*py);
	}
}

void 
MRAcquisitionData::xapyb
(const ISMRMRD::Acquisition& acq_x, complex_float_t a,
	ISMRMRD::Acquisition& acq_y, complex_float_t b)
{
	MRAcquisitionData::axpby(a, acq_x, b, acq_y);
}

void
MRAcquisitionData::xapyb
(const ISMRMRD::Acquisition& acq_x, const ISMRMRD::Acquisition& acq_a,
	ISMRMRD::Acquisition& acq_y, const ISMRMRD::Acquisition& acq_b)
{
	const complex_float_t* px;
	const complex_float_t* pa;
	complex_float_t* py;
	const complex_float_t* pb;
	for (px = acq_x.data_begin(), pa = acq_a.data_begin(),
		py = acq_y.data_begin(), pb = acq_b.data_begin();
		px != acq_x.data_end() && pa != acq_a.data_end(),
		py != acq_y.data_end() && pb != acq_b.data_end();
		px++, pa++, py++, pb++) {
		*py = (*pa) * (*px) + (*pb) * (*py);
	}
}

void
MRAcquisitionData::xapyb
(const ISMRMRD::Acquisition& acq_x, complex_float_t a,
    ISMRMRD::Acquisition& acq_y, const ISMRMRD::Acquisition& acq_b)
{
    const complex_float_t* px;
    complex_float_t* py;
    const complex_float_t* pb;
    for (px = acq_x.data_begin(),
        py = acq_y.data_begin(), pb = acq_b.data_begin();
        px != acq_x.data_end() &&
        py != acq_y.data_end() && pb != acq_b.data_end();
        px++, py++, pb++) {
        *py = a * (*px) + (*pb) * (*py);
    }
}

void
MRAcquisitionData::binary_op
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y, 
    complex_float_t (*f)(complex_float_t, complex_float_t))
{
    const complex_float_t* px;
    complex_float_t* py;
    for (px = acq_x.data_begin(), py = acq_y.data_begin();
        px != acq_x.data_end() && py != acq_y.data_end(); px++, py++) {
        *py = f(*px, *py);
    }
}

void
MRAcquisitionData::semibinary_op
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y, complex_float_t y,
    complex_float_t(*f)(complex_float_t, complex_float_t))
{
    const complex_float_t* px;
    complex_float_t* py;
    for (px = acq_x.data_begin(), py = acq_y.data_begin();
        px != acq_x.data_end() && py != acq_y.data_end(); px++, py++) {
        *py = f(*px, y);
    }
}

void
MRAcquisitionData::unary_op
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y,
    complex_float_t(*f)(complex_float_t))
{
    const complex_float_t* px;
    complex_float_t* py;
    for (px = acq_x.data_begin(), py = acq_y.data_begin();
        px != acq_x.data_end() && py != acq_y.data_end(); px++, py++) {
        *py = f(*px);
    }
}

void
MRAcquisitionData::multiply
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::binary_op(acq_x, acq_y, DataContainer::product<complex_float_t>);
}

void
MRAcquisitionData::multiply
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y, complex_float_t y)
{
    MRAcquisitionData::semibinary_op(acq_x, acq_y, y, DataContainer::product<complex_float_t>);
}

void
MRAcquisitionData::add
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y, complex_float_t y)
{
    MRAcquisitionData::semibinary_op(acq_x, acq_y, y, DataContainer::sum<complex_float_t>);
}

void
MRAcquisitionData::divide
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::binary_op(acq_x, acq_y, DataContainer::ratio<complex_float_t>);
}

void
MRAcquisitionData::maximum
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::binary_op(acq_x, acq_y, DataContainer::maxreal<complex_float_t>);
}

void
MRAcquisitionData::maximum
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y, complex_float_t y)
{
    MRAcquisitionData::semibinary_op(acq_x, acq_y, y, DataContainer::maxreal<complex_float_t>);
}

void
MRAcquisitionData::minimum
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::binary_op(acq_x, acq_y, DataContainer::minreal<complex_float_t>);
}

void
MRAcquisitionData::minimum
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y, complex_float_t y)
{
    MRAcquisitionData::semibinary_op(acq_x, acq_y, y, DataContainer::minreal<complex_float_t>);
}

void
MRAcquisitionData::power
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::binary_op(acq_x, acq_y, DataContainer::power);
}

void
MRAcquisitionData::power
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y, complex_float_t y)
{
    MRAcquisitionData::semibinary_op(acq_x, acq_y, y, DataContainer::power);
}

void
MRAcquisitionData::exp
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::unary_op(acq_x, acq_y, DataContainer::exp);
}

void
MRAcquisitionData::log
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::unary_op(acq_x, acq_y, DataContainer::log);
}

void
MRAcquisitionData::sqrt
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::unary_op(acq_x, acq_y, DataContainer::sqrt);
}

void
MRAcquisitionData::sign
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::unary_op(acq_x, acq_y, DataContainer::sign);
}

void
MRAcquisitionData::abs
(const ISMRMRD::Acquisition& acq_x, ISMRMRD::Acquisition& acq_y)
{
    MRAcquisitionData::unary_op(acq_x, acq_y, DataContainer::abs);
}

complex_float_t
MRAcquisitionData::dot
(const ISMRMRD::Acquisition& acq_a, const ISMRMRD::Acquisition& acq_b)
{
	const complex_float_t* pa;
	const complex_float_t* pb;
	complex_float_t z = 0;
	for (pa = acq_a.data_begin(), pb = acq_b.data_begin();
		pa != acq_a.data_end() && pb != acq_b.data_end(); pa++, pb++) {
		z += std::conj(*pb) * (*pa);
	}
	return z;
}

float 
MRAcquisitionData::norm(const ISMRMRD::Acquisition& acq_a)
{
	const complex_float_t* pa;
	float r = 0;
	for (pa = acq_a.data_begin(); pa != acq_a.data_end(); pa++) {
		complex_float_t z = std::conj(*pa) * (*pa);
		r += z.real();
	}
	r = std::sqrt(r);
	return r;
}

complex_float_t
MRAcquisitionData::sum(const ISMRMRD::Acquisition& acq_a)
{
    const complex_float_t* pa;
    complex_float_t z = 0;
    for (pa = acq_a.data_begin(); pa != acq_a.data_end(); pa++)
        z += *pa;
    return z;
}

complex_float_t
MRAcquisitionData::max(const ISMRMRD::Acquisition& acq_a)
{
    const complex_float_t* pa;
    complex_float_t z = 0;
    for (pa = acq_a.data_begin(); pa != acq_a.data_end(); pa++) {
        complex_float_t zi = *pa;
        float r = std::real(z);
        float ri = std::real(zi);
        if (ri > r)
            z = zi;
    }
    return z;
}

void
MRAcquisitionData::dot(const DataContainer& dc, void* ptr) const
{
	SIRF_DYNAMIC_CAST(const MRAcquisitionData, other, dc);
	int n = number();
	int m = other.number();
	complex_float_t z = 0;
	ISMRMRD::Acquisition a;
	ISMRMRD::Acquisition b;
	for (int i = 0, j = 0; i < n && j < m;) {
		if (!get_acquisition(i, a)) {
			i++;
			continue;
		}
		if (!other.get_acquisition(j, b)) {
			j++;
			continue;
		}
		z += MRAcquisitionData::dot(a, b);
		i++;
		j++;
	}
    complex_float_t* ptr_z = static_cast<complex_float_t*>(ptr);
    *ptr_z = z;
}

void
MRAcquisitionData::sum(void* ptr) const
{
    int n = number();
    complex_float_t z = 0;
    ISMRMRD::Acquisition a;
    for (int i = 0; i < n;) {
        if (!get_acquisition(i, a)) {
            i++;
            continue;
        }
        z += MRAcquisitionData::sum(a);
        i++;
    }
    complex_float_t* ptr_z = static_cast<complex_float_t*>(ptr);
    *ptr_z = z;
}

void
MRAcquisitionData::max(void* ptr) const
{
    int n = number();
    complex_float_t z = 0;
    ISMRMRD::Acquisition a;
    for (int i = 0; i < n;) {
        if (!get_acquisition(i, a)) {
            i++;
            continue;
        }
        complex_float_t zi = MRAcquisitionData::max(a);
        float r = std::real(z);
        float ri = std::real(zi);
        if (ri > r)
            z = zi;
        i++;
    }
    complex_float_t* ptr_z = static_cast<complex_float_t*>(ptr);
    *ptr_z = z;
}

void
MRAcquisitionData::axpby(
    const void* ptr_a, const DataContainer& a_x,
    const void* ptr_b, const DataContainer& a_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
    if (!x.sorted() || !y.sorted())
        THROW("a*x + b*y cannot be applied to unsorted x or y");
    complex_float_t a = *static_cast<const complex_float_t*>(ptr_a);
    complex_float_t b = *static_cast<const complex_float_t*>(ptr_b);
    int nx = x.number();
    int ny = y.number();
    ISMRMRD::Acquisition ax;
    ISMRMRD::Acquisition ay;
    ISMRMRD::Acquisition acq;
    bool isempty = (number() < 1);
    for (int ix = 0, iy = 0, k = 0; ix < nx && iy < ny;) {
        if (!x.get_acquisition(ix, ax)) {
            std::cout << ix << " ignored (ax)\n";
            ix++;
            continue;
        }
        if (!y.get_acquisition(iy, ay)) {
            std::cout << iy << " ignored (ay)\n";
            iy++;
            continue;
        }
        if (!isempty) {
            if (!get_acquisition(k, acq)) {
                std::cout << k << " ignored (acq)\n";
                k++;
                continue;
            }
        }
        MRAcquisitionData::axpby(a, ax, b, ay);
        if (isempty)
            append_acquisition(ay);
        else
            set_acquisition(k, ay);
        ix++;
        iy++;
        k++;
    }
    this->set_sorted(true);
    this->organise_kspace();
}

void
MRAcquisitionData::xapyb(
    const DataContainer& a_x, const DataContainer& a_a,
    const DataContainer& a_y, const DataContainer& a_b)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, a, a_a);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, b, a_b);
    if (!x.sorted() || !y.sorted() || !a.sorted() || !b.sorted())
        THROW("x*a + y*b cannot be applied to unsorted a, b, x or y");
    int nx = x.number();
    int ny = y.number();
    int na = a.number();
    int nb = b.number();
    ISMRMRD::Acquisition ax;
    ISMRMRD::Acquisition ay;
    ISMRMRD::Acquisition aa;
    ISMRMRD::Acquisition ab;
    ISMRMRD::Acquisition acq;
    bool isempty = (number() < 1);
    for (int ix = 0, iy = 0, ia = 0, ib = 0, k = 0;
        ix < nx && iy < ny && ia < na && ib < nb;) {
        if (!x.get_acquisition(ix, ax)) {
            std::cout << ix << " ignored (ax)\n";
            ix++;
            continue;
        }
        if (!y.get_acquisition(iy, ay)) {
            std::cout << iy << " ignored (ay)\n";
            iy++;
            continue;
        }
        if (!a.get_acquisition(ia, aa)) {
            std::cout << ia << " ignored (aa)\n";
            ia++;
            continue;
        }
        if (!b.get_acquisition(ib, ab)) {
            std::cout << ib << " ignored (ab)\n";
            ib++;
            continue;
        }
        if (!isempty) {
            if (!get_acquisition(k, acq)) {
                std::cout << k << " ignored (acq)\n";
                k++;
                continue;
            }
        }
        MRAcquisitionData::xapyb(ax, aa, ay, ab);
        if (isempty)
            append_acquisition(ay);
        else
            set_acquisition(k, ay);
        ix++;
        iy++;
        ia++;
        ib++;
        k++;
    }
    this->set_sorted(true);
    this->organise_kspace();
}

void
MRAcquisitionData::xapyb(
    const DataContainer& a_x, const void* ptr_a,
    const DataContainer& a_y, const DataContainer& a_b)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, b, a_b);
    if (!x.sorted() || !y.sorted() || !b.sorted())
        THROW("x*a + y*b cannot be applied to unsorted a, b, x or y");
    complex_float_t a = *static_cast<const complex_float_t*>(ptr_a);
    int nx = x.number();
    int ny = y.number();
    int nb = b.number();
    ISMRMRD::Acquisition ax;
    ISMRMRD::Acquisition ay;
    ISMRMRD::Acquisition ab;
    ISMRMRD::Acquisition acq;
    bool isempty = (number() < 1);
    for (int ix = 0, iy = 0, ib = 0, k = 0;
        ix < nx && iy < ny && ib < nb;) {
        if (!x.get_acquisition(ix, ax)) {
            std::cout << ix << " ignored (ax)\n";
            ix++;
            continue;
        }
        if (!y.get_acquisition(iy, ay)) {
            std::cout << iy << " ignored (ay)\n";
            iy++;
            continue;
        }
        if (!b.get_acquisition(ib, ab)) {
            std::cout << ib << " ignored (ab)\n";
            ib++;
            continue;
        }
        if (!isempty) {
            if (!get_acquisition(k, acq)) {
                std::cout << k << " ignored (acq)\n";
                k++;
                continue;
            }
        }
        MRAcquisitionData::xapyb(ax, a, ay, ab);
        if (isempty)
            append_acquisition(ay);
        else
            set_acquisition(k, ay);
        ix++;
        iy++;
        ib++;
        k++;
    }
    this->set_sorted(true);
    this->organise_kspace();
}

void
MRAcquisitionData::multiply(const DataContainer& a_x, const DataContainer& a_y)
{
	SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
	SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
	binary_op(x, y, MRAcquisitionData::multiply);
}

void
MRAcquisitionData::multiply(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, MRAcquisitionData::multiply);
}

void
MRAcquisitionData::add(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, MRAcquisitionData::add);
}

void
MRAcquisitionData::divide(const DataContainer& a_x, const DataContainer& a_y)
{
	SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
	SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
	binary_op(x, y, MRAcquisitionData::divide);
}

void
MRAcquisitionData::maximum(const DataContainer& a_x, const DataContainer& a_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
    binary_op(x, y, MRAcquisitionData::maximum);
}

void
MRAcquisitionData::maximum(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, MRAcquisitionData::maximum);
}

void
MRAcquisitionData::minimum(const DataContainer& a_x, const DataContainer& a_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
    binary_op(x, y, MRAcquisitionData::minimum);
}

void
MRAcquisitionData::minimum(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, MRAcquisitionData::minimum);
}

void
MRAcquisitionData::power(const DataContainer& a_x, const DataContainer& a_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
    binary_op(x, y, MRAcquisitionData::power);
}

void
MRAcquisitionData::power(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, MRAcquisitionData::power);
}

void
MRAcquisitionData::exp(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    unary_op(x, MRAcquisitionData::exp);
}

void
MRAcquisitionData::log(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    unary_op(x, MRAcquisitionData::log);
}

void
MRAcquisitionData::sqrt(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    unary_op(x, MRAcquisitionData::sqrt);
}

void
MRAcquisitionData::sign(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    unary_op(x, MRAcquisitionData::sign);
}

void
MRAcquisitionData::abs(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    unary_op(x, MRAcquisitionData::abs);
}

void
MRAcquisitionData::binary_op(
    const DataContainer& a_x, const DataContainer& a_y,
    void(*f)(const ISMRMRD::Acquisition&, ISMRMRD::Acquisition&))
{
	SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
	SIRF_DYNAMIC_CAST(const MRAcquisitionData, y, a_y);
	if (!x.sorted() || !y.sorted())
		THROW("binary algebraic operations cannot be applied to unsorted data");

	int nx = x.number();
	int ny = y.number();
	ISMRMRD::Acquisition ax;
	ISMRMRD::Acquisition ay;
	ISMRMRD::Acquisition acq;
	bool isempty = (number() < 1);
    for (int ix = 0, iy = 0, k = 0; ix < nx && iy < ny;) {
		if (!x.get_acquisition(ix, ax)) {
			std::cout << ix << " ignored (ax)\n";
			ix++;
			continue;
		}
		if (!y.get_acquisition(iy, ay)) {
			std::cout << iy << " ignored (ay)\n";
			iy++;
			continue;
		}
		if (!isempty) {
			if (!get_acquisition(k, acq)) {
				std::cout << k << " ignored (acq)\n";
				k++;
				continue;
			}
		}
        f(ax, ay);
		if (isempty)
			append_acquisition(ay);
		else
			set_acquisition(k, ay);
		ix++;
		iy++;
		k++;
	}
	this->set_sorted(true);
	this->organise_kspace();
}

void
MRAcquisitionData::semibinary_op(const DataContainer& a_x, complex_float_t y,
    void(*f)(const ISMRMRD::Acquisition&, ISMRMRD::Acquisition&, complex_float_t y))
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    if (!x.sorted())
        THROW("binary algebraic operations cannot be applied to unsorted data");

    int nx = x.number();
    ISMRMRD::Acquisition ax;
    ISMRMRD::Acquisition ay;
    ISMRMRD::Acquisition acq;
    bool isempty = (number() < 1);
    for (int ix = 0, k = 0; ix < nx;) {
        if (!x.get_acquisition(ix, ax)) {
            std::cout << ix << " ignored (ax)\n";
            ix++;
            continue;
        }
        if (!isempty) {
            if (!get_acquisition(k, acq)) {
                std::cout << k << " ignored (acq)\n";
                k++;
                continue;
            }
        }
        x.get_acquisition(ix, ay);
        f(ax, ay, y);
        if (isempty)
            append_acquisition(ay);
        else
            set_acquisition(k, ay);
        ix++;
        k++;
    }
    this->set_sorted(true);
    this->organise_kspace();
}

void
MRAcquisitionData::unary_op(const DataContainer& a_x,
    void(*f)(const ISMRMRD::Acquisition&, ISMRMRD::Acquisition&))
{
    SIRF_DYNAMIC_CAST(const MRAcquisitionData, x, a_x);
    if (!x.sorted())
        THROW("binary algebraic operations cannot be applied to unsorted data");

    int nx = x.number();
    ISMRMRD::Acquisition ax;
    ISMRMRD::Acquisition ay;
    ISMRMRD::Acquisition acq;
    bool isempty = (number() < 1);
    for (int ix = 0, k = 0; ix < nx;) {
        if (!x.get_acquisition(ix, ax)) {
            std::cout << ix << " ignored (ax)\n";
            ix++;
            continue;
        }
        if (!isempty) {
            if (!get_acquisition(k, acq)) {
                std::cout << k << " ignored (acq)\n";
                k++;
                continue;
            }
        }
        x.get_acquisition(ix, ay);
        f(ax, ay);
        if (isempty)
            append_acquisition(ay);
        else
            set_acquisition(k, ay);
        ix++;
        k++;
    }
    this->set_sorted(true);
    this->organise_kspace();
}

float
MRAcquisitionData::norm() const
{
	int n = number();
	float r = 0;
	ISMRMRD::Acquisition a;
	for (int i = 0; i < n; i++) {
		if (!get_acquisition(i, a)) {
			continue;
		}
		float s = MRAcquisitionData::norm(a);
		r += s*s;
	}
	return std::sqrt(r);
}


ISMRMRD::TrajectoryType
MRAcquisitionData::get_trajectory_type() const
{
    AcquisitionsInfo hdr_as_ai = acquisitions_info();
    ISMRMRD::IsmrmrdHeader hdr = hdr_as_ai.get_IsmrmrdHeader();

    if(hdr.encoding.size()!= 1)
        std::cout << "You have a file with " << hdr.encoding.size() << " encodings. Just the first one is picked." << std::endl;

    return hdr.encoding[0].trajectory;

}

void MRAcquisitionData::set_trajectory_type(const ISMRMRD::TrajectoryType type) 
{
    bool const argument_valid = type == ISMRMRD::TrajectoryType::CARTESIAN || \
                                    type == ISMRMRD::TrajectoryType::EPI || \
                                    type == ISMRMRD::TrajectoryType::GOLDENANGLE || \
                                    type == ISMRMRD::TrajectoryType::RADIAL || \
                                    type == ISMRMRD::TrajectoryType::SPIRAL || \
                                    type == ISMRMRD::TrajectoryType::OTHER;

    if(!argument_valid)
        throw std::runtime_error("The trajectory type you provided was invalid");

    ISMRMRD::IsmrmrdHeader hdr = acquisitions_info().get_IsmrmrdHeader();

    if(hdr.encoding.size()!= 1)
        std::cout << "You have a file with " << hdr.encoding.size() << " encodings. Just the first one is picked." << std::endl;

    hdr.encoding[0].trajectory = type;
    std::stringstream ss_hdr;
    ISMRMRD::serialize(hdr, ss_hdr);
    set_acquisitions_info(ss_hdr.str());
}

void
MRAcquisitionData::sort()
{
    sort_by_time();
}

void
MRAcquisitionData::sort_by_time()
{
    size_t const N = this->number();
    index_.resize(N);
    if (N == 0)
        std::cerr
        << "WARNING: cannot sort an empty container of acquisition data."
        << std::endl;
    else {
        std::vector<uint32_t> a;
        for (size_t i = 0; i < N; i++)
        {
            ISMRMRD::Acquisition acq;
            get_acquisition(i, acq);
            a.push_back(acq.acquisition_time_stamp());
        }
        int* index = &index_[0];
        std::iota(index, index + N, 0);
        std::stable_sort
        (index, index + N, [&a](int i, int j) {return (a[i] < a[j]); });
    }

    this->organise_kspace();
    sorted_ = true;

}

std::vector<KSpaceSubset::SetType > MRAcquisitionData::get_kspace_order() const
{
    if(this->is_empty())
        throw LocalisedException("Your acquisition data object contains no data, so no order is determined." , __FILE__, __LINE__);
    else if(this->sorting_.size() == 0)
        throw LocalisedException("The kspace is not sorted yet. Please call organise_kspace(), sort() or sort_by_time() first." , __FILE__, __LINE__);
    

    std::vector<KSpaceSubset::SetType > output;
    for(unsigned i = 0; i<sorting_.size(); ++i)
    {
        if(!sorting_.at(i).get_idx_set().empty())
               output.push_back(sorting_.at(i).get_idx_set());
    }
    return output;
}

static int get_num_enc_states( const ISMRMRD::Optional<ISMRMRD::Limit>& enc_lim)
{
	int num_states =1;

	if(enc_lim.is_present())
	{
	    ISMRMRD::Limit lim = enc_lim.get();
		num_states = lim.maximum - lim.minimum +1;
	}

	return num_states;
}

void MRAcquisitionData::organise_kspace()
{
    std::vector<KSpaceSubset>().swap(this->sorting_);

    const ISMRMRD::IsmrmrdHeader header = this->acquisitions_info().get_IsmrmrdHeader();

    auto encoding_vector = header.encoding;

    if(encoding_vector.size()>1)
        throw LocalisedException("Curerntly only one encoding is supported. You supplied multiple in one ismrmrd file.", __FUNCTION__, __LINE__);

    ISMRMRD::Encoding encoding = encoding_vector[0];
    ISMRMRD::EncodingLimits enc_lims = encoding.encodingLimits;

    int NAvg    = get_num_enc_states(enc_lims.average); 
    int NSlice  = get_num_enc_states(enc_lims.slice); 
    int NCont   = get_num_enc_states(enc_lims.contrast);
    int NPhase  = get_num_enc_states(enc_lims.phase); 
    int NRep    = get_num_enc_states(enc_lims.repetition);
    int NSet    = get_num_enc_states(enc_lims.set);
    int NSegm = 1; // lim_segm.maximum    - lim_segm.minimum +1; // this has no correspondence in the header of the image of course. currently no sorting wrt to this

    for(int ia= 0; ia <NAvg; ia++)
    for(int is= 0; is <NSlice; is++)
    for(int ic= 0; ic <NCont; ic++)
    for(int ip= 0; ip <NPhase; ip++)
    for(int ir= 0; ir <NRep; ir++)
    for(int iset= 0; iset <NSet; iset++)
    for(int iseg=0;   iseg<NSegm; ++iseg)
    {
        KSpaceSubset::TagType tag{ia, is, ic, ip, ir, iset, iseg};
        for(int i=7; i<tag.size(); ++i)
            tag[i]=0; // ignore user ints so far

        KSpaceSubset sorting(tag);
        this->sorting_.push_back(sorting);
    }

    ISMRMRD::Acquisition acq;
    for(int i=0; i<this->number(); ++i)
    {
        this->get_acquisition(i, acq);

        KSpaceSubset::TagType tag = KSpaceSubset::get_tag_from_acquisition(acq);
        int access_idx = (((((tag[0] * NSlice + tag[1])*NCont + tag[2])*NPhase + tag[3])*NRep + tag[4])*NSet + tag[5])*NSegm + tag[6];
        this->sorting_.at(access_idx).add_idx_to_set(i);
    }
    this->sorting_.erase(
                std::remove_if(sorting_.begin(), sorting_.end(),[](const KSpaceSubset& s){return s.get_idx_set().empty();}),
                sorting_.end());
}


std::vector<int> MRAcquisitionData::get_flagged_acquisitions_index(const std::vector<ISMRMRD::ISMRMRD_AcquisitionFlags> flags) const
{
    std::vector<int> flags_true_index;

    if(flags.empty())
        return flags_true_index;

    ISMRMRD::Acquisition acq;

    for(int i=0; i<this->number(); ++i)
    {
        this->get_acquisition(i, acq);
        bool one_flag_is_set = false;
        
        for(auto it: flags)
            one_flag_is_set = (one_flag_is_set || acq.isFlagSet(it));
        
        if(one_flag_is_set)
            flags_true_index.push_back(i);
    }

    return flags_true_index;
}

std::vector<int> MRAcquisitionData::get_slice_encoding_index(const unsigned kspace_encode_step_2) const
{
    std::vector<int> slice_encode_index;

    ISMRMRD::Acquisition acq;

    for(int i=0; i<this->number(); ++i)
    {
        this->get_acquisition(i, acq);
        if( acq.idx().kspace_encode_step_2 == kspace_encode_step_2)
            slice_encode_index.push_back(i);
    }

    return slice_encode_index;
}


void MRAcquisitionData::get_subset(MRAcquisitionData& subset, const std::vector<int> subset_idx) const
{
    subset.set_acquisitions_info(this->acquisitions_info());

    if(subset.number()>0)
        throw LocalisedException("Please pass an empty MRAcquisitionnData container to store the subset in", __FUNCTION__, __LINE__);

    ISMRMRD::Acquisition acq;
    for(int i=0; i<subset_idx.size(); ++i)
    {
        this->get_acquisition(subset_idx[i], acq);
        subset.append_acquisition(acq);
    }
}

void MRAcquisitionData::set_subset(const MRAcquisitionData& subset, const std::vector<int> subset_idx)
{
    if(subset.number() != subset_idx.size())
        throw LocalisedException("Number of subset positions and number of acquisitions in subset don't match.", __FILE__, __LINE__);

    ISMRMRD::Acquisition acq;
    for(int i=0; i<subset_idx.size(); ++i)
    {
        subset.get_acquisition(i, acq);
        this->set_acquisition(subset_idx[i], acq);
    }
}

AcquisitionsVector*
AcquisitionsVector::clone_impl() const
{
	AcquisitionsVector* ptr_ad =
		new AcquisitionsVector(this->acqs_info_);
	for (int i = 0; i < number(); i++) {
		ISMRMRD::Acquisition acq;
		get_acquisition(i, acq);
		ptr_ad->append_acquisition(acq);
	}
	ptr_ad->set_sorted(sorted());
	if (sorted())
		ptr_ad->organise_kspace();
	return ptr_ad;
}

void
AcquisitionsVector::empty()
{
	acqs_.clear();
    index_.clear();
}

void
AcquisitionsVector::conjugate_impl()
{
    int na = number();
    for (int a = 0, i = 0; a < na; a++) {
        int ia = index(a);
        ISMRMRD::Acquisition& acq = *acqs_[ia];
        unsigned int nc = acq.active_channels();
        unsigned int ns = acq.number_of_samples();
        for (int c = 0; c < nc; c++)
            for (int s = 0; s < ns; s++, i++)
                acq.data(s, c) = std::conj(acq.data(s, c));
    }
}

void
AcquisitionsVector::set_data(const complex_float_t* z, int all)
{
	int na = number();
	for (int a = 0, i = 0; a < na; a++) {
		int ia = index(a);
		ISMRMRD::Acquisition& acq = *acqs_[ia];
		if (!all && TO_BE_IGNORED(acq)) {
			std::cout << "ignoring acquisition " << ia << '\n';
			continue;
		}
		unsigned int nc = acq.active_channels();
		unsigned int ns = acq.number_of_samples();
		for (int c = 0; c < nc; c++)
			for (int s = 0; s < ns; s++, i++)
				acq.data(s, c) = z[i];
	}
}

void
AcquisitionsVector::copy_acquisitions_data(const MRAcquisitionData& ac)
{
	ISMRMRD::Acquisition acq_dst;
	ISMRMRD::Acquisition acq_src;
	int na = number();
	ASSERT(na == ac.number(), "copy source and destination sizes differ");
	for (int a = 0, i = 0; a < na; a++) {
		ac.get_acquisition(a, acq_src);
		ISMRMRD::Acquisition& acq_dst = *acqs_[a];
		unsigned int nc = acq_dst.active_channels();
		unsigned int ns = acq_dst.number_of_samples();
		ASSERT(nc == acq_src.active_channels(), 
			"copy source and destination coil numbers differ");
		ASSERT(ns == acq_src.number_of_samples(), 
			"copy source and destination samples numbers differ");
		for (int c = 0; c < nc; c++)
			for (int s = 0; s < ns; s++, i++)
				acq_dst.data(s, c) = acq_src.data(s, c);
	}
}

KSpaceSubset::TagType KSpaceSubset::get_tag_from_img(const CFImage& img)
{
    TagType tag;

    tag[0] = img.getAverage();
    tag[1] = img.getSlice();
    tag[2] = img.getContrast();
    tag[3] = img.getPhase();
    tag[4] = img.getRepetition();
    tag[5] = img.getSet();
    tag[6] = 0; //segments area always zero

    for(int i=0; i<ISMRMRD::ISMRMRD_Constants::ISMRMRD_USER_INTS; ++i)
        tag[7+i] = 0; //img.getUserInt(i);

    return tag;
}


KSpaceSubset::TagType KSpaceSubset::get_tag_from_acquisition(ISMRMRD::Acquisition acq)
{
    TagType tag;
    tag[0] = acq.idx().average;
    tag[1] = acq.idx().slice;
    tag[2] = acq.idx().contrast;
    tag[3] = acq.idx().phase;
    tag[4] = acq.idx().repetition;
    tag[5] = acq.idx().set;
    tag[6] = 0; //acq.idx().segment;

    for(int i=7; i<tag.size(); ++i)
        tag[i]= 0; //acq.idx().user[i];

    return tag;
}

void KSpaceSubset::print_tag(const TagType& tag)
{
    std::cout << "(";

    for(int i=0; i<tag.size();++i)
        std::cout << tag[i] <<",";

    std::cout << ")" << std::endl;
}

void KSpaceSubset::print_acquisition_tag(ISMRMRD::Acquisition acq)
{
    TagType tag = get_tag_from_acquisition(acq);
    print_tag(tag);
}

void
GadgetronImageData::dot(const DataContainer& dc, void* ptr) const
{
	SIRF_DYNAMIC_CAST(const GadgetronImageData, ic, dc);
	complex_float_t z = 0;
	for (unsigned int i = 0; i < number() && i < ic.number(); i++) {
		const ImageWrap& u = image_wrap(i);
		const ImageWrap& v = ic.image_wrap(i);
		z += u.dot(v);
	}
    complex_float_t* ptr_z = static_cast<complex_float_t*>(ptr);
    *ptr_z = z;
}

void
GadgetronImageData::sum(void* ptr) const
{
    complex_float_t z = 0;
    for (unsigned int i = 0; i < number(); i++) {
        const ImageWrap& u = image_wrap(i);
        complex_float_t t = u.sum();
        z += t;
    }
    complex_float_t* ptr_z = static_cast<complex_float_t*>(ptr);
    *ptr_z = z;
}

void
GadgetronImageData::max(void* ptr) const
{
    complex_float_t z = 0;
    for (unsigned int i = 0; i < number(); i++) {
        const ImageWrap& wi = image_wrap(i);
        complex_float_t zi = wi.max();
        float r = std::real(z);
        float ri = std::real(zi);
        if (ri > r)
            z = zi;
    }
    complex_float_t* ptr_z = static_cast<complex_float_t*>(ptr);
    *ptr_z = z;
}

void
GadgetronImageData::axpby(
const void* ptr_a, const DataContainer& a_x,
const void* ptr_b, const DataContainer& a_y)
{
    complex_float_t a = *static_cast<const complex_float_t*>(ptr_a);
    complex_float_t b = *static_cast<const complex_float_t*>(ptr_b);
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
	SIRF_DYNAMIC_CAST(const GadgetronImageData, y, a_y);
	unsigned int nx = x.number();
	unsigned int ny = y.number();
	if (nx != ny)
		THROW("ImageData sizes mismatch in axpby");
	unsigned int n = number();
	if (n > 0) {
		if (n != nx)
			THROW("ImageData sizes mismatch in axpby");
		for (unsigned int i = 0; i < nx; i++)
			image_wrap(i).axpby(a, x.image_wrap(i), b, y.image_wrap(i));
	}
	else {
		for (unsigned int i = 0; i < nx; i++) {
			const ImageWrap& u = x.image_wrap(i);
			const ImageWrap& v = y.image_wrap(i);
			ImageWrap w(u);
			w.axpby(a, u, b, v);
			append(w);
		}
	}
	this->set_meta_data(x.get_meta_data());
}

void
GadgetronImageData::binary_op(
    const DataContainer& a_x, const DataContainer& a_y,
    complex_float_t(*f)(complex_float_t, complex_float_t))
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    SIRF_DYNAMIC_CAST(const GadgetronImageData, y, a_y);
    unsigned int nx = x.number();
    unsigned int ny = y.number();
    if (nx != ny)
        THROW("ImageData sizes mismatch in binary_op");
    unsigned int n = number();
    if (n > 0) {
        if (n != nx)
            THROW("ImageData sizes mismatch in binary_op");
        for (unsigned int i = 0; i < nx && i < ny; i++)
            image_wrap(i).binary_op(x.image_wrap(i), y.image_wrap(i), f);
    }
    else {
        for (unsigned int i = 0; i < nx && i < ny; i++) {
            ImageWrap w(x.image_wrap(i));
            w.binary_op(x.image_wrap(i), y.image_wrap(i), f);
            append(w);
        }
    }
    this->set_meta_data(x.get_meta_data());
}

void
GadgetronImageData::semibinary_op(
    const DataContainer& a_x, complex_float_t y,
    complex_float_t(*f)(complex_float_t, complex_float_t))
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    unsigned int nx = x.number();
    unsigned int n = number();
    if (n > 0) {
        if (n != nx)
            THROW("ImageData sizes mismatch in semibinary_op");
        for (unsigned int i = 0; i < nx; i++)
            image_wrap(i).semibinary_op(x.image_wrap(i), y, f);
    }
    else {
        for (unsigned int i = 0; i < nx; i++) {
            ImageWrap w(x.image_wrap(i));
            w.semibinary_op(x.image_wrap(i), y, f);
            append(w);
        }
    }
    this->set_meta_data(x.get_meta_data());
}

void
GadgetronImageData::unary_op(const DataContainer& a_x,
    complex_float_t(*f)(complex_float_t))
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    unsigned int nx = x.number();
    unsigned int n = number();
    if (n > 0) {
        if (n != nx)
            THROW("ImageData sizes mismatch in semibinary_op");
        for (unsigned int i = 0; i < nx; i++)
            image_wrap(i).unary_op(x.image_wrap(i), f);
    }
    else {
        for (unsigned int i = 0; i < nx; i++) {
            ImageWrap w(x.image_wrap(i));
            w.unary_op(x.image_wrap(i), f);
            append(w);
        }
    }
    this->set_meta_data(x.get_meta_data());
}

void
GadgetronImageData::multiply(const DataContainer& a_x, const DataContainer& a_y)
{
	SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
	SIRF_DYNAMIC_CAST(const GadgetronImageData, y, a_y);
    binary_op(x, y, DataContainer::product<complex_float_t>);
}

void
GadgetronImageData::multiply(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, DataContainer::product<complex_float_t>);
}

void
GadgetronImageData::add(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, DataContainer::sum<complex_float_t>);
}

void
GadgetronImageData::divide(const DataContainer& a_x, const DataContainer& a_y)
{
	SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
	SIRF_DYNAMIC_CAST(const GadgetronImageData, y, a_y);
    binary_op(x, y, DataContainer::ratio<complex_float_t>);
}

void
GadgetronImageData::maximum(
    const DataContainer& a_x,
    const DataContainer& a_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    SIRF_DYNAMIC_CAST(const GadgetronImageData, y, a_y);
    binary_op(x, y, DataContainer::maxreal<complex_float_t>);
}

void
GadgetronImageData::maximum(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, DataContainer::maxreal<complex_float_t>);
}

void
GadgetronImageData::minimum(
    const DataContainer& a_x,
    const DataContainer& a_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    SIRF_DYNAMIC_CAST(const GadgetronImageData, y, a_y);
    binary_op(x, y, DataContainer::minreal<complex_float_t>);
}

void
GadgetronImageData::minimum(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, DataContainer::minreal<complex_float_t>);
}

void
GadgetronImageData::power(
    const DataContainer& a_x,
    const DataContainer& a_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    SIRF_DYNAMIC_CAST(const GadgetronImageData, y, a_y);
    binary_op(x, y, DataContainer::power);
}

void
GadgetronImageData::power(const DataContainer& a_x, const void* ptr_y)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    complex_float_t y = *static_cast<const complex_float_t*>(ptr_y);
    semibinary_op(x, y, DataContainer::power);
}

void
GadgetronImageData::exp(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    unary_op(x, DataContainer::exp);
}

void
GadgetronImageData::log(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    unary_op(x, DataContainer::log);
}

void
GadgetronImageData::sqrt(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    unary_op(x, DataContainer::sqrt);
}

void
GadgetronImageData::sign(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    unary_op(x, DataContainer::sign);
}

void
GadgetronImageData::abs(const DataContainer& a_x)
{
    SIRF_DYNAMIC_CAST(const GadgetronImageData, x, a_x);
    unary_op(x, DataContainer::abs);
}

float
GadgetronImageData::norm() const
{
	float r = 0;
	for (unsigned int i = 0; i < number(); i++) {
		const ImageWrap& u = image_wrap(i);
		float s = u.norm();
		r += s*s;
	}
	r = std::sqrt(r);
	return r;
}

void
GadgetronImageData::fill(float s)
{
	for (unsigned int i = 0; i < number(); i++) {
		ImageWrap& u = image_wrap(i);
		u.fill(s);
	}
}

void
GadgetronImageData::scale(float s)
{
	for (unsigned int i = 0; i < number(); i++) {
		ImageWrap& u = image_wrap(i);
		u.scale(s);
	}
}

void
GadgetronImagesVector::sort()
{
	typedef std::array<float, 3> tuple;
	int ni = number();
	tuple t;
	std::vector<tuple> vt;
	for (int i = 0; i < ni; i++) {
      ImageWrap& iw = image_wrap(i);
      ISMRMRD::ImageHeader& head = iw.head();
		// It is crucial to sort the images first by their projection onto the slice direction
        // and only then consider other dimensions.
        // In the function this->reorient() the computation of the position of the 2D slices
        // requires them to be ordered with respect to their projection onto the slice direction to
        // ensure that they are located next to each other.
        t[0] = -( head.position[0] * head.slice_dir[0] +
                head.position[1] * head.slice_dir[1]   +
                head.position[2] * head.slice_dir[2]   );
        t[1] = head.contrast;
        t[2] = head.repetition;

		vt.push_back(t);
#ifndef NDEBUG
        std::cout << "Before sorting. Image " << i << "/" << ni <<  ", Projection: " << t[0] << ", Contrast: " << t[1] << ", Repetition: " << t[2] << "\n";
#endif
	}

	index_.resize(ni);
	Multisort::sort(vt, &index_[0] );
	sorted_ = true;

	// quick fix for the problem of compatibility with image data iterators
	std::vector<shared_ptr<ImageWrap> > sorted_images;
	for (int i = 0; i < ni; i++)
		sorted_images.push_back(sptr_image_wrap(i));
	images_ = sorted_images;
	index_.resize(0);

#ifndef NDEBUG
    std::cout << "After sorting...\n";
    for (int i = 0; i < ni; i++) {
      ImageWrap& iw = image_wrap(i);
      ISMRMRD::ImageHeader& head = iw.head();
		// Calculate the projection of the position in the slice direction
        t[0] = head.position[0] * head.slice_dir[0] +
               head.position[1] * head.slice_dir[1] +
               head.position[2] * head.slice_dir[2];
        t[1] = head.contrast;
        t[2] = head.repetition;
        
        std::cout << "Image " << i << "/" << ni <<  ", Projection: " << t[0] << ", Contrast: " << t[1] << ", Repetition: " << t[2] << "\n";

	}
#endif
}

shared_ptr<std::vector<std::string> >
group_names_sptr(const char* filename)
{
	hid_t    file;
	hid_t    root;
	hid_t    group;
	herr_t   err;
	herr_t   status;
	hsize_t  nobj;
	const int MAX_NAME = 1024;
	char group_name[MAX_NAME];
	char var_name[MAX_NAME];
	shared_ptr<std::vector<std::string> >
		sptr_names(new std::vector<std::string>);
	std::vector<std::string>& names = *sptr_names;

	file = H5Fopen(filename, H5F_ACC_RDWR, H5P_DEFAULT);
	if (file < 0)
		return sptr_names;
	root = H5Gopen(file, "/", H5P_DEFAULT);
	if (root < 0) {
		status = H5Fclose(file);
		return sptr_names;
	}
	H5Gget_objname_by_idx(root, 0, group_name, (size_t)MAX_NAME);
	names.push_back(std::string(group_name));
	group = H5Gopen(root, group_name, H5P_DEFAULT);
	err = H5Gget_num_objs(group, &nobj);
	//printf("%d objects\n", nobj);
	for (int i = 0; i < nobj; i++) {
		H5Gget_objname_by_idx(group, (hsize_t)i,
			var_name, (size_t)MAX_NAME);
		names.push_back(std::string(var_name));
	}

	H5Gclose(group);
	H5Gclose(root);
	status = H5Fclose(file);

	return sptr_names;
}

int
GadgetronImageData::read(std::string filename, std::string variable, int iv) 
{
	int vsize = variable.size();
	shared_ptr<std::vector<std::string> > sptr_names;
	sptr_names = group_names_sptr(filename.c_str());
	std::vector<std::string>& names = *sptr_names;
	int ng = names.size();
	const char* group = names[0].c_str();
	printf("group %s\n", group);
        Mutex mtx;
	for (int ig = 0; ig < ng; ig++) {
		const char* var = names[ig].c_str();
		if (!ig)
			continue;

		printf("variable %s\n", var);
		if (vsize > 0)
			if (strcmp(var, variable.c_str()))
				continue;
		if (iv > 0)
			if (ig != iv)
				continue;
		if (strcmp(var, "xml") == 0)
			continue;

		mtx.lock();

		ISMRMRD::ISMRMRD_Dataset dataset;
		ISMRMRD::ISMRMRD_Image im;
		ismrmrd_init_dataset(&dataset, filename.c_str(), group);
		ismrmrd_open_dataset(&dataset, false);
		int num_im = ismrmrd_get_number_of_images(&dataset, var);
		std::cout << "number of images: " << num_im << '\n';
		ismrmrd_init_image(&im);
		ismrmrd_read_image(&dataset, var, 0, &im);
		printf("image data type: %d\n", im.head.data_type);
		ismrmrd_cleanup_image(&im);
		ismrmrd_close_dataset(&dataset);

		shared_ptr<ISMRMRD::Dataset> sptr_dataset
			(new ISMRMRD::Dataset(filename.c_str(), group, false));

		// ISMRMRD throws an error if no XML is present.
		try {
			sptr_dataset->readHeader(this->acqs_info_);
		}
		catch (const std::exception &error) {
		}

		mtx.unlock();

		for (int i = 0; i < num_im; i++) {
			shared_ptr<ImageWrap> sptr_iw(new ImageWrap(im.head.data_type, *sptr_dataset, var, i));
			//sptr_iw->read(*sptr_dataset, var, i);
			append(*sptr_iw);
			//images_.push_back(sptr_iw);
		}
		if (vsize > 0 && strcmp(var, variable.c_str()) == 0)
			break;
		if (iv > 0 && ig == iv)
			break;
	}

	this->set_up_geom_info();
	return 0;
}

void
GadgetronImageData::write(const std::string &filename, const std::string &groupname, const bool dicom) const
{
	if (number() < 1)
		return;

    // If not DICOM
    if (!dicom) {
        Mutex mtx;
        std::ifstream file;
        mtx.lock();
        file.open(filename.c_str());
        if (file.good()) {
            file.close();
            int err = std::remove(filename.c_str());
            if (err)
                std::cerr << "deleting " << filename.c_str() << " failed, appending...\n";
        }
        file.close();
        mtx.unlock();
        // If the groupname hasn't been set, use the current date and time.
        std::string group = groupname;
        if (group.empty())
            group = get_date_time_string();
        mtx.lock();
        ISMRMRD::Dataset dataset(filename.c_str(), group.c_str());
        dataset.writeHeader(acqs_info_.c_str());
        mtx.unlock();
        for (unsigned int i = 0; i < number(); i++) {
            const ImageWrap& iw = image_wrap(i);
            iw.write(dataset);
        }
    }
    // If DICOM
    else {
        ImagesProcessor ip(true, filename);
        ip.process(*this);
    }
}

void
GadgetronImageData::conjugate_impl()
{
    for (unsigned int i = 0; i < number(); i++)
        image_wrap(i).conjugate();
}

void
GadgetronImageData::get_data(complex_float_t* data) const
{
	int dim[4];
	for (unsigned int i = 0; i < number(); i++) {
		const ImageWrap& iw = image_wrap(i);
		size_t n = iw.get_dim(dim);
		iw.get_complex_data(data);
		data += n;
	}
}

void
GadgetronImageData::set_data(const complex_float_t* z)
{
	int dim[4];
	for (unsigned int i = 0; i < number(); i++) {
		ImageWrap& iw = image_wrap(i);
		size_t n = iw.get_dim(dim);
		iw.set_complex_data(z);
		z += n;
	}
}

void
GadgetronImageData::get_real_data(float* data) const
{
	int dim[4];
	for (unsigned int i = 0; i < number(); i++) {
		const ImageWrap& iw = image_wrap(i);
		size_t n = iw.get_dim(dim);
		iw.get_data(data);
		data += n;
	}
}

void
GadgetronImageData::set_real_data(const float* z)
{
	int dim[4];
	for (unsigned int i = 0; i < number(); i++) {
		ImageWrap& iw = image_wrap(i);
		size_t n = iw.get_dim(dim);
		iw.set_data(z);
		z += n;
	}
}

void
GadgetronImageData::set_meta_data(const AcquisitionsInfo &acqs_info)
{
    acqs_info_ = acqs_info;
    this->set_up_geom_info();
}

GadgetronImagesVector::GadgetronImagesVector(const MRAcquisitionData& ad, const bool coil_resolved)
{

    ISMRMRD::IsmrmrdHeader hdr = ad.acquisitions_info().get_IsmrmrdHeader();

    unsigned int const num_coil_channels = coil_resolved ? hdr.acquisitionSystemInformation.get().receiverChannels.get() : 1;

    std::vector<ISMRMRD::Encoding> enc_vec = hdr.encoding;
    ISMRMRD::Encoding enc = enc_vec[0];
    ISMRMRD::EncodingSpace rec_space = enc.reconSpace;

    ISMRMRD::MatrixSize rawdata_recon_matrix = rec_space.matrixSize;
    ISMRMRD::FieldOfView_mm rawdata_recon_FOV = rec_space.fieldOfView_mm;
    
    auto sort_idx = ad.get_kspace_order();

    ISMRMRD::Acquisition acq;

    for(int i=0; i<sort_idx.size(); ++i)
    {

        CFImage img(rawdata_recon_matrix.x, rawdata_recon_matrix.y, rawdata_recon_matrix.z, num_coil_channels);
        img.setFieldOfView(rawdata_recon_FOV.x, rawdata_recon_FOV.y, rawdata_recon_FOV.z);

        sirf::AcquisitionsVector subset; // subset container must be empty so it is created inside the loop scope
        ad.get_subset(subset, sort_idx[i]);
        // all subsets are self-consistent and non-empty so it suffices to populate img header from the 0st acquisition
        subset.get_acquisition(0, acq);
        match_img_header_to_acquisition(img, acq);

        for(auto it=img.begin(); it!=img.end(); ++it)
            *it = complex_float_t(0.f,0.f);

        this->append(img);
    }

    set_meta_data(ad.acquisitions_info());
}    


GadgetronImagesVector::GadgetronImagesVector
(const GadgetronImagesVector& images) :
images_()
{
	SIRF_DYNAMIC_CAST(const GadgetronImageData, imgs, images);
	set_meta_data(imgs.get_meta_data());
	for (unsigned int i = 0; i < images.number(); i++) {
		const ImageWrap& u = images.image_wrap(i);
		append(u);
	}
    this->set_meta_data(images.get_meta_data());
    this->set_up_geom_info();
}

GadgetronImagesVector::GadgetronImagesVector
(GadgetronImagesVector& images, const char* attr, const char* target) : 
images_()
{
	SIRF_DYNAMIC_CAST(const GadgetronImageData, imgs, images);
	set_meta_data(imgs.get_meta_data());
	for (unsigned int i = 0; i < images.number(); i++) {
		const ImageWrap& u = images.image_wrap(i);
		std::string atts = u.attributes();
		ISMRMRD::MetaContainer mc;
		ISMRMRD::deserialize(atts.c_str(), mc);
		size_t l = mc.length(attr);
		std::string value;
		for (int j = 0; j < l; j++) {
			if (j)
				value += " ";
			value += mc.as_str(attr, j);
		}
		//std::cout << value.c_str() << '\n';
		if (sirf::iequals(value, target))
			append(u);
	}
    this->set_meta_data(images.get_meta_data());
    this->set_up_geom_info();
}

shared_ptr<GadgetronImageData>
GadgetronImagesVector::abs() const
{
	GadgetronImagesVector* ptr_iv = new GadgetronImagesVector;
	for (int i = 0; i < number(); i++) {
		ptr_iv->append(image_wrap(i).abs());
	}
    ptr_iv->set_meta_data(this->get_meta_data());
    return shared_ptr<GadgetronImageData>(ptr_iv);
}

shared_ptr<GadgetronImageData>
GadgetronImagesVector::real() const
{
    GadgetronImagesVector* ptr_iv = new GadgetronImagesVector;
    for (int i = 0; i < number(); i++) {
        ptr_iv->append(image_wrap(i).real());
    }
    ptr_iv->set_meta_data(this->get_meta_data());
    return shared_ptr<GadgetronImageData>(ptr_iv);
}

void
GadgetronImagesVector::set_image_type(int image_type)
{
    for (int i = 0; i < number(); i++) {
        ISMRMRD::ImageHeader& head = image_wrap(i).head();
        head.image_type = image_type;
    }
}

void
GadgetronImagesVector::get_data(complex_float_t* data) const
{
	//std::copy(begin(), end(), data);
	//std::cout << "trying new const image wrap iterator...\n";
	GadgetronImagesVector::Iterator_const stop = end();
	GadgetronImagesVector::Iterator_const iter = begin();
	for (; iter != stop; ++iter, ++data)
		*data = (*iter).complex_float();
}

void
GadgetronImagesVector::set_data(const complex_float_t* data)
{
	//int dim[4];
	//size_t n = number();
	//get_image_dimensions(0, dim);
	//n *= dim[0];
	//n *= dim[1];
	//n *= dim[2];
	//n *= dim[3];
	//std::copy(data, data + n, begin());
	//std::cout << "trying new image wrap iterator...\n";
	GadgetronImagesVector::Iterator stop = end();
	GadgetronImagesVector::Iterator iter = begin();
	for (; iter != stop; ++iter, ++data)
		*iter = *data;
}

void
GadgetronImagesVector::get_real_data(float* data) const
{
	GadgetronImagesVector::Iterator_const stop = end();
	GadgetronImagesVector::Iterator_const iter = begin();
	for (; iter != stop; ++iter, ++data)
		*data = *iter;
}

void
GadgetronImagesVector::set_real_data(const float* data)
{
	GadgetronImagesVector::Iterator stop = end();
	GadgetronImagesVector::Iterator iter = begin();
	for (; iter != stop; ++iter, ++data)
		*iter = *data;
}

void sirf::match_img_header_to_acquisition(CFImage& img, const ISMRMRD::Acquisition& acq) 
{
    auto acq_hdr = acq.getHead();
    auto idx = acq_hdr.idx;

    img.setAverage(idx.average);
    img.setSlice(idx.slice);
    img.setContrast(idx.contrast);
    img.setPhase(idx.phase);
    img.setRepetition(idx.repetition);
    img.setSet(idx.set);

    img.setReadDirection(acq_hdr.read_dir[0], acq_hdr.read_dir[1], acq_hdr.read_dir[2]);
    img.setPhaseDirection(acq_hdr.phase_dir[0], acq_hdr.phase_dir[1], acq_hdr.phase_dir[2]);
    img.setSliceDirection(acq_hdr.slice_dir[0], acq_hdr.slice_dir[1], acq_hdr.slice_dir[2]);

    img.setPosition(acq_hdr.position[0], acq_hdr.position[1], acq_hdr.position[2]);
    img.setPatientTablePosition(acq_hdr.patient_table_position[0], acq_hdr.patient_table_position[1], acq_hdr.patient_table_position[2]);
}

static bool is_unit_vector(const float * const vec)
{
    return std::abs(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2] - 1.F) < 1.e-4F;
}

static bool are_vectors_equal(const float * const vec1, const float * const vec2)
{
    for (int i=0; i<3; ++i)
        if (std::abs(vec1[i] - vec2[i]) > 1.e-4F)
            return false;
    return true;
}

static void print_slice_directions(const std::vector<shared_ptr<ImageWrap> > &images)
{
    std::cout << "\nGadgetronImagesVector::set_up_geom_info(): Slice direction alters between different slices. Expected it to be constant.\n";
    for (unsigned im=0; im<images.size(); ++im) {
        ISMRMRD::ImageHeader &ih = images[im]->head();
        float *sd = ih.slice_dir;
        std::cout << "Slice dir " << im << ": [" << sd[0] << ", " << sd[1] << ", " << sd[2] << "]\n";
    }
}

static void print_slice_distances(const std::vector<shared_ptr<ImageWrap> > &images)
{
    std::cout << "\nGadgetronImagesVector::set_up_geom_info(): Slice distances alters between slices. Expected it to be constant.\n";
    for (unsigned im=0; im<images.size()-1; ++im) {
        ISMRMRD::ImageHeader &ih1 = images[ im ]->head();
        ISMRMRD::ImageHeader &ih2 = images[im+1]->head();
        float projection_of_position_in_slice_dir_1 = ih1.position[0] * ih1.slice_dir[0] +
                ih1.position[1] * ih1.slice_dir[1] +
                ih1.position[2] * ih1.slice_dir[2];
        float projection_of_position_in_slice_dir_2 = ih2.position[0] * ih2.slice_dir[0] +
                ih2.position[1] * ih2.slice_dir[1] +
                ih2.position[2] * ih2.slice_dir[2];
        std::cout << "Spacing " << im << ": " << projection_of_position_in_slice_dir_1 - projection_of_position_in_slice_dir_2 << "\n";
    }
}

void
GadgetronImagesVector::print_header(const unsigned im_num)
{
    // Get image
    ISMRMRD::ImageHeader &ih = image_wrap(im_num).head();
    std::cout << "\n";
    std::cout << "phase:                  " << ih.phase                  << "\n";
    std::cout << "slice:                  " << ih.slice                  << "\n";
    std::cout << "average:                " << ih.average                << "\n";
    std::cout << "version:                " << ih.version                << "\n";
    std::cout << "channels:               " << ih.channels               << "\n";
    std::cout << "contrast:               " << ih.contrast               << "\n";
    std::cout << "data_type:              " << ih.data_type              << "\n";
    std::cout << "image_type:             " << ih.image_type             << "\n";
    std::cout << "repetition:             " << ih.repetition             << "\n";
    std::cout << "image_index:            " << ih.image_index            << "\n";
    std::cout << "measurement_uid:        " << ih.measurement_uid        << "\n";
    std::cout << "measurement_uid:        " << ih.measurement_uid        << "\n";
    std::cout << "image_series_index:     " << ih.image_series_index     << "\n";
    std::cout << "attribute_string_len:   " << ih.attribute_string_len   << "\n";
    std::cout << "acquisition_time_stamp: " << ih.acquisition_time_stamp << "\n";
    std::cout << "user_int:               "; for (int i=0;i<8;++i) std::cout << ih.user_int[i]               << " "; std::cout << "\n";
    std::cout << "user_float:             "; for (int i=0;i<8;++i) std::cout << ih.user_float[i]             << " "; std::cout << "\n";
    std::cout << "position:               "; for (int i=0;i<3;++i) std::cout << ih.position[i]               << " "; std::cout << "\n";
    std::cout << "read_dir:               "; for (int i=0;i<3;++i) std::cout << ih.read_dir[i]               << " "; std::cout << "\n";
    std::cout << "phase_dir:              "; for (int i=0;i<3;++i) std::cout << ih.phase_dir[i]              << " "; std::cout << "\n";
    std::cout << "slice_dir:              "; for (int i=0;i<3;++i) std::cout << ih.slice_dir[i]              << " "; std::cout << "\n";
    std::cout << "matrix_size:            "; for (int i=0;i<3;++i) std::cout << ih.matrix_size[i]            << " "; std::cout << "\n";
    std::cout << "field_of_view:          "; for (int i=0;i<3;++i) std::cout << ih.field_of_view[i]          << " "; std::cout << "\n";
    std::cout << "physiology_time_stamp:  "; for (int i=0;i<3;++i) std::cout << ih.physiology_time_stamp[i]  << " "; std::cout << "\n";
    std::cout << "patient_table_position: "; for (int i=0;i<3;++i) std::cout << ih.patient_table_position[i] << " "; std::cout << "\n";

    if (!acqs_info_.empty()) {
        std::cout << "XML data:\n";
        std::cout << acqs_info_.c_str() << "\n";
    }
}

bool GadgetronImagesVector::is_complex() const {
    // If any of the wraps are complex, return true.
    for (unsigned i=0; i<number(); ++i)
        if (image_wrap(i).is_complex())
            return true;
    return false;
}

void GadgetronImagesVector::reorient(const VoxelisedGeometricalInfo3D &geom_info_out)
{
    const VoxelisedGeometricalInfo3D &geom_info_in = *this->get_geom_info_sptr();

    // if input geom info matches output, nothing to do
    if (geom_info_in == geom_info_out)
        return;

    // Check can do reorient
    ImageData::can_reorient(geom_info_in, geom_info_out, true);

    // ------------------------------------------------------ //
    // Do the reorienting!
    // ------------------------------------------------------ //

    if (number() < 1)
        return;

    if (!this->sorted())
        this->sort();

    uint16_t number_slices = 0;

    for (unsigned im=1; im<number(); ++im) {
        ISMRMRD::ImageHeader &ih = image_wrap(im).head();
        number_slices = (ih.slice > number_slices) ? ih.slice : number_slices; 
    }
    number_slices += 1; // account for starting counting at zero.

    // loop over all images in stack
    for (unsigned im=0; im<number(); ++im) {
        // Get image header
        ISMRMRD::ImageHeader &ih = image_wrap(im).head();

        // Read, phase and slice directions
        auto direction = geom_info_out.get_direction();
        for (unsigned axis=0; axis<3; ++axis) {
            ih.read_dir[axis]  = -direction[axis][0];
            ih.phase_dir[axis] = -direction[axis][1];
            ih.slice_dir[axis] = -direction[axis][2];
        }

        // FOV
        auto spacing = geom_info_out.get_spacing();
        auto size = geom_info_out.get_size();
        
        // Read and phase FOV are matrix-size * voxel size
        for(unsigned i=0; i<2; ++i)
            ih.field_of_view[i] = spacing[i] * size[i];
        // 2D slices should only have the slice width as a FOV along slice encoding
        // for 3D number_slices = 1 and no correction is necessary
        ih.field_of_view[2] = spacing[2] * size[2] / number_slices; 
        
        // Position
        auto offset = geom_info_out.get_offset();
        for (unsigned i=0; i<3; ++i)
        {
            ih.position[i] = offset[i]
                    + direction[i][0] * (ih.field_of_view[0] / 2.0f)
                    + direction[i][1] * (ih.field_of_view[1] / 2.0f)
                    + direction[i][2] * (ih.field_of_view[2] / 2.0f); // for 2D stacks this is half the slice thickness
                     
            // For 2D stacks the position is the position of the first slice plus the slice number in the slice direction.
            // However, temporally subsequently acquired slices are usually not next to each other in space
            // but at a distance to ensure relaxation of the magnetisation.
            // this->sort() called above sorts the images first by the projection onto the slice direction.
            // Hence (im % number_slices) gives the geometrical order of slices while
            // using ih.slice to iterate over slices would give them in order of acquisition time instead. 
            ih.position[i] += direction[i][2] * (im % number_slices) * geom_info_out.get_spacing()[2]; 
        }
    }

    // set up geom info
    this->set_up_geom_info();

    // Check reorient success
    if (*this->get_geom_info_sptr() != geom_info_out)
        throw std::runtime_error("GadgetronImagesVector::reorient failed");
}

float get_projection_of_position_in_slice(const ISMRMRD::ImageHeader &ih)
{
    return ih.position[0] * ih.slice_dir[0] +
            ih.position[1] * ih.slice_dir[1] +
            ih.position[2] * ih.slice_dir[2];
}

float get_slice_spacing(const ISMRMRD::ImageHeader &ih1, const ISMRMRD::ImageHeader &ih2)
{
    return std::abs(get_projection_of_position_in_slice(ih1) - get_projection_of_position_in_slice(ih2));
}

void
GadgetronImagesVector::set_up_geom_info()
{
#ifndef NDEBUG
    std::cout << "\nSetting up geometrical info for GadgetronImagesVector...\n";
#endif

    if (number() < 1)
        return;

    if (!this->sorted())
        this->sort();

    // Patient position not necessary as read, phase and slice directions
    // are already in patient coordinates
#if 0
    ISMRMRD::IsmrmrdHeader image_header = this->acqs_info_.get_IsmrmrdHeader();
    if (!image_header.measurementInformation.is_present())
        std::cout << "\nGadgetronImagesVector::set_up_geom_info: Patient position not present. Assuming HFS\n";
    else if (image_header.measurementInformation.get().patientPosition.compare("HFS") != 0)
        std::cout << "\nGadgetronImagesVector::set_up_geom_info: Currently only implemented for HFS. TODO (easy fix)\n";
#endif
    // Get image
    ISMRMRD::ImageHeader &ih1 = image_wrap(0).head();

    // Check that read, phase and slice directions are all unit vectors
    if (!(is_unit_vector(ih1.read_dir) && is_unit_vector(ih1.phase_dir) && is_unit_vector(ih1.slice_dir))) {
//#ifndef NDEBUG
        std::cout << "\nGadgetronImagesVector::set_up_geom_info(): read_dir, phase_dir and slice_dir should all be unit vectors.\n";
//#endif
        return;
    }

    // Check that the read, phase and slice directions are constant
    uint16_t number_slices = ih1.slice; 

    for (unsigned im=1; im<number(); ++im) {
        ISMRMRD::ImageHeader &ih = image_wrap(im).head();
        
        // record which is the largest slice index
        // this allows to differentiate between slice number and this->number() as the
        // latter also includes different contrasts, phases, repetitions etc. that have
        // no geometrical meaning
        number_slices = (ih.slice > number_slices) ? ih.slice : number_slices; 

        if (!(are_vectors_equal(ih1.read_dir,ih.read_dir) && are_vectors_equal(ih1.phase_dir,ih.phase_dir) && are_vectors_equal(ih1.slice_dir,ih.slice_dir))) {
            std::cout << "\nGadgetronImagesVector::set_up_geom_info(): read_dir, phase_dir and slice_dir should be constant over slices.\n";
            return;
        }
    }
    number_slices += 1; // we start counting at 0

    // Size
    // For the z-direction.
    // If it's a 3d image, matrix_size[2] == num voxels
    // If it's a 2d image, matrix_size[2] == 1, and number of slices is given by number_slices.
    // We will check below that if 3D data is present, slices are not repeated as we do not yet cover this case.
    // Luckily this is usually not happening.
    VoxelisedGeometricalInfo3D::Size size;
    for(unsigned i=0; i<3; ++i)
        size[i] = ih1.matrix_size[i];
    
    // Spacing
    //for 2D case: size[2] = 1 and ih1.field_of_view[2] = excited slice thickness
    VoxelisedGeometricalInfo3D::Spacing spacing;
    for(unsigned i=0; i<3; ++i)
        spacing[i] = ih1.field_of_view[i] / size[i]; 

    bool const is_2d_stack = (number_slices > 1) && (size[2] == 1);

    if( (number_slices > 1) && (size[2] > 1))
        throw LocalisedException("You try to set up the geometry information for 3D data that contains multiple slices. This special case is unavailable." , __FILE__, __LINE__);

    if( is_2d_stack )        
            size[2] = number_slices;

    // If there are more than 1 slices, then take the size of the voxel
    // in the z-direction to be the distance between voxel centres (this
    // accounts for under-sampled data (and also over-sampled).
    if (is_2d_stack) {

        // Calculate the spacing!
        ISMRMRD::ImageHeader &ih2 = image_wrap(1).head();

        const float tolerance_mm = 0.01f;
        if( std::abs(spacing[2] - get_slice_spacing(ih1, ih2)) > tolerance_mm )
        {
            std::cout << "\nGadgetronImagesVector::set_up_geom_info(). "
                        "Warning, you set up geometry for slices whose width is not their distance."
                        "This setup does probably not account for overlaps or gaps between slices.\n";
        }
        // just making sure they are the same, as opposed to "up to tolerance"
        spacing[2] = get_slice_spacing(ih1, ih2);

        // Check: Loop over all images, and check that spacing is more-or-less constant
        for (unsigned im=0; im<number()-1; ++im) {

            ISMRMRD::ImageHeader &ih1 = image_wrap( im ).head();
            ISMRMRD::ImageHeader &ih2 = image_wrap(im+1).head();

            // 2. Check that spacing is constant
            float new_spacing = get_slice_spacing(ih1, ih2);
            if (std::abs(spacing[2]-new_spacing) > 1.e-4F) {
                print_slice_distances(images_);
                return;
            }
        }
    }

    //size[2] *= number();

    // Make sure we're looking at the first image
    ih1 = image_wrap( 0 ).head();
    
    // Direction
    VoxelisedGeometricalInfo3D::DirectionMatrix direction;
    for (unsigned axis=0; axis<3; ++axis) {
        direction[axis][0] = -ih1.read_dir[axis];
        direction[axis][1] = -ih1.phase_dir[axis];
        direction[axis][2] = -ih1.slice_dir[axis];
    }

    // Offset
    VoxelisedGeometricalInfo3D::Offset offset;
    for (unsigned i=0; i<3; ++i)
        offset[i] = ih1.position[i]
                - direction[i][0] * (ih1.field_of_view[0] / 2.0f)
                - direction[i][1] * (ih1.field_of_view[1] / 2.0f)
                - direction[i][2] * (ih1.field_of_view[2] / 2.0f);

    // Initialise the geom info shared pointer
    this->set_geom_info(std::make_shared<VoxelisedGeometricalInfo3D>
                (offset,spacing,size,direction));
}

void 
CoilImagesVector::calculate(const MRAcquisitionData& ad)
{
    if(ad.get_trajectory_type() == ISMRMRD::TrajectoryType::CARTESIAN)
        this->sptr_enc_ = std::make_shared<sirf::CartesianFourierEncoding>();
    else if(ad.get_trajectory_type() == ISMRMRD::TrajectoryType::OTHER)
    {
        ASSERT(ad.get_trajectory_dimensions()>0, "You should set a type ISMRMRD::TrajectoryType::OTHER trajectory before calling the calculate method with dimension > 0.");
    #ifdef GADGETRON_TOOLBOXES_AVAILABLE
    #warning "Compiling non-cartesian code into coil sensitivity class"
        this->sptr_enc_ = std::make_shared<sirf::RPEFourierEncoding>();
    #else
        throw std::runtime_error("Non-cartesian reconstruction is not supported, but your file contains ISMRMRD::TrajectoryType::OTHER data.");
    #endif
    }
    else if(ad.get_trajectory_type() == ISMRMRD::TrajectoryType::RADIAL || 
            ad.get_trajectory_type() == ISMRMRD::TrajectoryType::GOLDENANGLE ||
            ad.get_trajectory_type() == ISMRMRD::TrajectoryType::SPIRAL)
	{
		ASSERT(ad.get_trajectory_dimensions()>0, "You should set a type ISMRMRD::TrajectoryType::RADIAL, ISMRMRD::TrajectoryType::GOLDENANGLE or ISMRMRD::TrajectoryType::SPIRAL trajectory before calling the calculate method with dimension > 0.");
	#ifdef GADGETRON_TOOLBOXES_AVAILABLE
	#warning "Compiling non-cartesian code into coil sensitivity class"
		this->sptr_enc_ = std::make_shared<sirf::NonCartesian2DEncoding>();
	#else
		throw std::runtime_error("Non-cartesian reconstruction is not supported, but your file contains ISMRMRD::TrajectoryType::RADIAL data.");
	#endif
	}
    else
        throw std::runtime_error("Only cartesian or OTHER type of trajectory are available.");

    std::unique_ptr<MRAcquisitionData> uptr_calib_data = this->extract_calibration_data(ad);
    
    this->set_meta_data(uptr_calib_data->acquisitions_info());
    auto sort_idx = uptr_calib_data->get_kspace_order();

    for(int i=0; i<sort_idx.size(); ++i)
    {
        sirf::AcquisitionsVector subset;
        uptr_calib_data->get_subset(subset, sort_idx[i]);

		CFImage* img_ptr = new CFImage();
		ImageWrap iw(ISMRMRD::ISMRMRD_DataTypes::ISMRMRD_CXFLOAT, img_ptr);
		this->sptr_enc_->backward(*img_ptr, subset);

        this->append(iw);
    }
}


std::unique_ptr<MRAcquisitionData> CoilImagesVector::extract_calibration_data(const MRAcquisitionData& ad) const
{
    using ISMRMRD::ISMRMRD_AcquisitionFlags;

    const std::vector<ISMRMRD_AcquisitionFlags> 
                calibration_flags{ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION,
                                  ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION_AND_IMAGING};
    
    std::unique_ptr<MRAcquisitionData> uptr_calib_ad = ad.clone();

    if(ad.get_trajectory_type() == ISMRMRD::TrajectoryType::CARTESIAN)
    {   
        std::vector<int> idx_calib_acquisitions = ad.get_flagged_acquisitions_index(calibration_flags);
        
        if(idx_calib_acquisitions.size() < 1)
            return uptr_calib_ad;

        uptr_calib_ad->empty();
        ad.get_subset(*(uptr_calib_ad), idx_calib_acquisitions);
        uptr_calib_ad->sort_by_time();
    }
    return uptr_calib_ad;
}

CFImage CoilSensitivitiesVector::get_csm_as_cfimage(size_t const i) const
{
    auto sptr_iw = this->sptr_image_wrap(i);
    if(sptr_iw->type() != ISMRMRD::ISMRMRD_CXFLOAT)
        throw LocalisedException("The coilmaps must be supplied as a complex float ismrmrd image, i.e. type = ISMRMRD::ISMRMRD_CXFLOAT." , __FILE__, __LINE__);

    const void* ptr_cf_img = sptr_iw->ptr_image();
    return CFImage(*( (CFImage*)ptr_cf_img));
}

CFImage CoilSensitivitiesVector::get_csm_as_cfimage(const KSpaceSubset::TagType tag, const int offset) const
{
    for(int i=0; i<this->items();++i)
    {
        size_t const access_idx = ((offset + i) % this->items());
        CFImage csm_img = get_csm_as_cfimage(access_idx);
        KSpaceSubset::TagType tag_csm = KSpaceSubset::get_tag_from_img(csm_img);

        if(tag_csm[1] == tag[1] && tag_csm[2]==0) //tag[1]=slice, tag[2]=contrast
            return csm_img;
    }

    throw LocalisedException("No coilmap with this tag was in the coilsensitivity container.",   __FILE__, __LINE__);
}

void CoilSensitivitiesVector::forward(GadgetronImageData& img, const GadgetronImageData& combined_img)const
{
    if(combined_img.items() != this->items() )
        throw LocalisedException("The number of coilmaps does not equal the number of images to which they should be applied to.",   __FILE__, __LINE__);

    if(!combined_img.check_dimension_consistency())
       throw LocalisedException("The image dimensions in the source image container are not consistent.",   __FILE__, __LINE__);

    if(combined_img.dimensions()["c"] != 1)
        throw LocalisedException("The source image has more than one channel.",   __FILE__, __LINE__);

    img.set_meta_data( combined_img.get_meta_data());
    img.clear_data();

    this->coilchannels_from_combined_image(img, combined_img);
}

void CoilSensitivitiesVector::coilchannels_from_combined_image(GadgetronImageData& img, const GadgetronImageData& combined_img) const
{
    for(size_t i_img=0; i_img<combined_img.items(); ++i_img)
    {
        const ImageWrap& iw_src = combined_img.image_wrap(i_img);
        const CFImage* ptr_src_img = static_cast<const CFImage*>(iw_src.ptr_image());

        CFImage coilmap = get_csm_as_cfimage( KSpaceSubset::get_tag_from_img(*ptr_src_img), i_img);

        CFImage* ptr_dst_img = new CFImage(coilmap);
		sirf::ImageWrap iw_dst(ISMRMRD::ISMRMRD_CXFLOAT, ptr_dst_img);

        ptr_dst_img->setHead((*ptr_src_img).getHead());
        ptr_dst_img->setNumberOfChannels(coilmap.getNumberOfChannels());

        size_t const Nx = ptr_dst_img->getMatrixSizeX();
        size_t const Ny = ptr_dst_img->getMatrixSizeY();
        size_t const Nz = ptr_dst_img->getMatrixSizeZ();
        size_t const Nc = ptr_dst_img->getNumberOfChannels();

        for( size_t nc=0;nc<Nc ; nc++)
        for( size_t nz=0;nz<Nz ; nz++)
        for( size_t ny=0;ny<Ny ; ny++)
        for( size_t nx=0;nx<Nx ; nx++)
        {
            (*ptr_dst_img)(nx, ny, nz, nc) =
            *(ptr_src_img->getDataPtr() + nx + Nx * (ny + Ny * nz))
            * coilmap(nx, ny, nz, nc);
        }

        img.append(iw_dst);
    }
}

void CoilSensitivitiesVector::backward(GadgetronImageData& combined_img, const GadgetronImageData& img)const
{

    if(img.items() != this->items() )
           throw LocalisedException("The number of coilmaps does not equal the number of images to be combined.",   __FILE__, __LINE__);

       // check for matching dimensions
       if(!img.check_dimension_consistency())
           throw LocalisedException("The image dimensions in the source image container are not consistent.",   __FILE__, __LINE__);


       combined_img.set_meta_data(img.get_meta_data());
       combined_img.clear_data();

       this->combine_images_with_coilmaps(combined_img, img);
}

void CoilSensitivitiesVector::combine_images_with_coilmaps(GadgetronImageData& combined_img, const GadgetronImageData& img) const
{
    std::vector<int> img_dims(4);
    img.get_image_dimensions(0, &img_dims[0]);

    for(size_t i_img=0; i_img<img.items(); ++i_img)
    {
        const ImageWrap& iw_src = img.image_wrap(i_img);
        const CFImage* ptr_src_img = static_cast<const CFImage*>(iw_src.ptr_image());

		// const void* vptr_src_img = iw_src.ptr_image();
        // const CFImage* ptr_src_img = static_cast<const CFImage*>(vptr_src_img);
		
        CFImage coilmap= get_csm_as_cfimage(KSpaceSubset::get_tag_from_img(*ptr_src_img), i_img);

        int const Nx = (int)coilmap.getMatrixSizeX();
        int const Ny = (int)coilmap.getMatrixSizeY();
        int const Nz = (int)coilmap.getMatrixSizeZ();
        int const Nc = (int)coilmap.getNumberOfChannels();

        std::vector<int> csm_dims{Nx, Ny, Nz, Nc};

        if( img_dims != csm_dims)
            throw LocalisedException("The data dimensions of the image don't match the sensitivity maps.",   __FILE__, __LINE__);

        
		CFImage* ptr_dst_img = new CFImage(Nx, Ny, Nz, 1); //urgh this is so horrible
		sirf::ImageWrap iw_dst(ISMRMRD::ISMRMRD_CXFLOAT, ptr_dst_img );
		
		ptr_dst_img->setHead(ptr_src_img->getHead());
		ptr_dst_img->setNumberOfChannels(1);

		for(auto it=ptr_dst_img->begin(); it!=ptr_dst_img->end(); ++it)	
			*it = complex_float_t(0.f,0.f);
		
		// conjugate the coilmap
		for(auto it=coilmap.begin(); it!=coilmap.end(); ++it)
			*it = std::conj(*it);


		// multiply with image
		std::transform( coilmap.begin(), coilmap.end(),
						ptr_src_img->getDataPtr(), coilmap.begin(), 
						std::multiplies<complex_float_t>());

        for( size_t nc=0;nc<Nc ; nc++)
        for( size_t nz=0;nz<Nz ; nz++)
        for( size_t ny=0;ny<Ny ; ny++)
        for( size_t nx=0;nx<Nx ; nx++)
	        ptr_dst_img->operator() (nx, ny, nz, 0) += coilmap(nx,ny,nz,nc);

        combined_img.append(iw_dst);
    }
}


void 
CoilSensitivitiesVector::calculate(CoilImagesVector& iv)
{

    this->empty();

    for(int i_img=0; i_img<iv.items();++i_img)
    {
        shared_ptr<ImageWrap> sptr_iw = iv.sptr_image_wrap(i_img);

        int dim[4];
        sptr_iw->get_dim(dim);
        std::vector<size_t> img_dims;
        for(int i_dim=0; i_dim<4; ++i_dim)
            img_dims.push_back(dim[i_dim]);

        ISMRMRD::NDArray<complex_float_t> cm(img_dims);
        ISMRMRD::NDArray<float> img(img_dims);
        ISMRMRD::NDArray<complex_float_t> csm(img_dims);

        sptr_iw->get_complex_data(cm.getDataPtr());
        this->calculate_csm(cm, img, csm);

        ImageWrap iw_output(*sptr_iw);
        iw_output.set_complex_data(csm.getDataPtr());
        this->append(iw_output);
    }
}

void CoilSensitivitiesVector::calculate_csm
                    (ISMRMRD::NDArray<complex_float_t>& cm,
                     ISMRMRD::NDArray<float>& img,
                     ISMRMRD::NDArray<complex_float_t>& csm)
{
    int ndims = cm.getNDim();
    const size_t* dims = cm.getDims();
    unsigned int readout = (unsigned int)dims[0];
    unsigned int ny = (unsigned int)dims[1];
    unsigned int nz = (unsigned int)dims[2];
    unsigned int nc = (unsigned int)dims[3];
    unsigned int nx = (unsigned int)img.getDims()[0];

    std::vector<size_t> cm0_dims;
    cm0_dims.push_back(nx);
    cm0_dims.push_back(ny);
    cm0_dims.push_back(nz);
    cm0_dims.push_back(nc);

    ISMRMRD::NDArray<complex_float_t> cm0(cm0_dims);
    for (unsigned int c = 0; c < nc; c++) {
        for (unsigned int z = 0; z < nz; z++) {
            for (unsigned int y = 0; y < ny; y++) {
                for (unsigned int x = 0; x < nx; x++) {
                    uint16_t xout = x + (readout - nx) / 2;
                    cm0(x, y, z, c) = cm(xout, y, z, c);
                }
            }
        }
    }

    int* object_mask = new int[nx*ny*nz];
    memset(object_mask, 0, nx*ny*nz * sizeof(int));

    ISMRMRD::NDArray<complex_float_t> v(cm0);
    ISMRMRD::NDArray<complex_float_t> w(cm0);

    float* ptr_img = img.getDataPtr();
    for (unsigned int z = 0; z < nz; z++) {
        for (unsigned int y = 0; y < ny; y++) {
            for (unsigned int x = 0; x < nx; x++) {
                float r = 0.0;
                for (unsigned int c = 0; c < nc; c++) {
                    float s = std::abs(cm0(x, y, z, c));
                    r += s*s;
                }
                img(x, y, z) = (float)std::sqrt(r);
            }
        }
    }

    float max_im = max_(nx, ny, nz, ptr_img);
    float small_grad = max_im * 2 / (nx + ny + 0.0f);
    for (int i = 0; i < 3; i++)
        smoothen_(nx, ny, nz, nc, v.getDataPtr(), w.getDataPtr(), 0, 1);
    float noise = max_diff_(nx, ny, nz, nc, small_grad,
        v.getDataPtr(), cm0.getDataPtr());
    mask_noise_(nx, ny, nz, ptr_img, noise, object_mask);

    for (int i = 0; i < csm_smoothness_; i++)
        smoothen_(nx, ny, nz, nc, cm0.getDataPtr(), w.getDataPtr(), //0, 1);
            object_mask, 1);

    for (unsigned int z = 0; z < nz; z++) {
        for (unsigned int y = 0; y < ny; y++) {
            for (unsigned int x = 0; x < nx; x++) {
                float r = 0.0;
                for (unsigned int c = 0; c < nc; c++) {
                    float s = std::abs(cm0(x, y, z, c));
                    r += s*s;
                }
                img(x, y, z) = (float)std::sqrt(r);
            }
        }
    }

    for (unsigned int z = 0, i = 0; z < nz; z++) {
        for (unsigned int y = 0; y < ny; y++) {
            for (unsigned int x = 0; x < nx; x++, i++) {
                float r = img(x, y, z);
                float s;
                if (r != 0.0 && object_mask[i])
                    s = (float)(1.0 / r);
                else
                    s = 0.0;
                complex_float_t zs(s, 0.0);
                for (unsigned int c = 0; c < nc; c++) {
                    csm(x, y, z, c) = zs * cm0(x, y, z, c);
                }
            }
        }
    }

    delete[] object_mask;
}



void CoilSensitivitiesVector::mask_noise_
(int nx, int ny, int nz, float* u, float noise, int* mask)
{
    int i = 0;
    for (int iz = 0; iz < nz; iz++)
        for (int iy = 0; iy < ny; iy++)
            for (int ix = 0; ix < nx; ix++, i++) {
            float t = fabs(u[i]);
            mask[i] = (t > noise);
        }
}

void
CoilSensitivitiesVector::smoothen_
(int nx, int ny, int nz, int nc,
    complex_float_t* u, complex_float_t* v,
    int* obj_mask, int w)
{
    const complex_float_t ONE(1.0, 0.0);
    const complex_float_t TWO(2.0, 0.0);
    for (int ic = 0, i = 0; ic < nc; ic++)
        for (int iz = 0, k = 0; iz < nz; iz++)
            for (int iy = 0; iy < ny; iy++)
                for (int ix = 0; ix < nx; ix++, i++, k++) {
                    if (obj_mask && !obj_mask[k]) {
                        v[i] = u[i];
                        continue;
                    }
                    int n = 0;
                    complex_float_t r(0.0, 0.0);
                    complex_float_t s(0.0, 0.0);
                    for (int jy = -w; jy <= w; jy++)
                        for (int jx = -w; jx <= w; jx++) {
                            if (ix + jx < 0 || ix + jx >= nx)
                                continue;
                            if (iy + jy < 0 || iy + jy >= ny)
                                continue;
                            int j = i + jx + jy*nx;
                            int l = k + jx + jy*nx;
                            if (i != j && (!obj_mask || obj_mask[l])) {
                                n++;
                                r += ONE;
                                s += u[j];
                            }
                        }
                    if (n > 0)
                        v[i] = (u[i] + s / r) / TWO;
                    else
                        v[i] = u[i];
                }
    memcpy(u, v, nx*ny*nz*nc * sizeof(complex_float_t));
}

float
CoilSensitivitiesVector::max_(int nx, int ny, int nz, float* u)
{
    float r = 0.0;
    int i = 0;
    for (int iz = 0; iz < nz; iz++)
        for (int iy = 0; iy < ny; iy++)
            for (int ix = 0; ix < nx; ix++, i++) {
            float t = fabs(u[i]);
            if (t > r)
                r = t;
        }
    return r;
}

float
CoilSensitivitiesVector::max_diff_
(int nx, int ny, int nz, int nc, float small_grad,
    complex_float_t* u, complex_float_t* v)
{
    int nxy = nx*ny;
    int nxyz = nxy*nz;
    float s = 0.0f;
    for (int ic = 0; ic < nc; ic++) {
        for (int iz = 0; iz < nz; iz++) {
            for (int iy = 1; iy < ny - 1; iy++) {
                for (int ix = 1; ix < nx - 1; ix++) {
                    int i = ix + nx*iy + nxy*iz + nxyz*ic;
                    float gx = std::abs(u[i + 1] - u[i - 1]) / 2.0f;
                    float gy = std::abs(u[i + nx] - u[i - nx]) / 2.0f;
                    float g = (float)std::sqrt(gx*gx + gy*gy);
                    float si = std::abs(u[i] - v[i]);
                    if (g <= small_grad && si > s)
                        s = si;
                }
            }
        }
    }
    return s;
}

