/*
SyneRBI Synergistic Image Reconstruction Framework (SIRF)
Copyright 2015 - 2023 Rutherford Appleton Laboratory STFC
Copyright 2019 - 2023 University College London
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

#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include <ismrmrd/ismrmrd.h>
#include <ismrmrd/dataset.h>

#include "sirf/common/iequals.h"
#include "sirf/iUtilities/DataHandle.h"
#include "sirf/Gadgetron/cgadgetron_shared_ptr.h"
#include "sirf/Gadgetron/gadgetron_data_containers.h"
#include "sirf/Gadgetron/gadgetron_client.h"
//#include "iutilities.h" // causes problems with Matlab (cf. the same message below)
#include "sirf/Gadgetron/cgadgetron_p.h"
#include "sirf/Gadgetron/gadgetron_x.h"
#include "sirf/Gadgetron/gadget_lib.h"
#include "sirf/Gadgetron/chain_lib.h"
#include "sirf/Gadgetron/TrajectoryPreparation.h"
// #include "sirf/Gadgetron/FourierEncoding.h"

#if GADGETRON_TOOLBOXES_AVAILABLE
    #include "sirf/Gadgetron/NonCartesianEncoding.h"
#endif


using namespace gadgetron;
using namespace sirf;

#define GRAB 1

#define NEW_OBJECT_HANDLE(T) new ObjectHandle<T>(shared_ptr<T>(new T))
#define NEW_GADGET(G) if (sirf::iequals(name, G::class_name())) \
	return NEW_OBJECT_HANDLE(G)
#define NEW_GADGET_CHAIN(C) if (sirf::iequals(name, C::class_name())) \
	return NEW_OBJECT_HANDLE(C)
#define SPTR_FROM_HANDLE(Object, X, H) \
	shared_ptr<Object> X; getObjectSptrFromHandle<Object>(H, X);

shared_ptr<std::mutex> Mutex::sptr_mutex_;

static void*
unknownObject(const char* obj, const char* name, const char* file, int line)
{
	DataHandle* handle = new DataHandle;
	std::string error = "Unknown ";
	error += obj;
	error += " '";
	error += name;
	error += "'";
	ExecutionStatus status(error.c_str(), file, line);
	handle->set(0, &status);
	return (void*)handle;
}

static void*
parameterNotFound(const char* name, const char* file, int line)
{
	DataHandle* handle = new DataHandle;
	std::string error = "Parameter ";
	error += name;
	error += " not found";
	ExecutionStatus status(error.c_str(), file, line);
	handle->set(0, &status);
	return (void*)handle;
}

static void*
fileNotFound(const char* name, const char* file, int line)
{
	DataHandle* handle = new DataHandle;
	std::string error = "File ";
	error += name;
	error += " not found";
	ExecutionStatus status(error.c_str(), file, line);
	handle->set(0, &status);
	return (void*)handle;
}

static bool 
file_exists(const std::string& filename)
{
	std::ifstream file;
	file.open(filename.c_str());
	if (file.good()) {
		file.close();
		return true;
	}
	return false;
}

extern "C"
void* cGT_newObject(const char* name)
{
	try {
		if (sirf::iequals(name, "Mutex"))
			return NEW_OBJECT_HANDLE(Mutex);
		if (sirf::iequals(name, "GTConnector"))
			return NEW_OBJECT_HANDLE(GTConnector);
		if (sirf::iequals(name, "CoilImages"))
			return NEW_OBJECT_HANDLE(CoilImagesVector);
        if (sirf::iequals(name, "AcquisitionModel"))
			return NEW_OBJECT_HANDLE(MRAcquisitionModel);
		NEW_GADGET_CHAIN(GadgetChain);
		NEW_GADGET_CHAIN(AcquisitionsProcessor);
		NEW_GADGET_CHAIN(ImagesReconstructor);
		NEW_GADGET_CHAIN(ImagesProcessor);
		NEW_GADGET_CHAIN(RemoveOversamplingProcessor);
		NEW_GADGET_CHAIN(ExtractRealImagesProcessor);
		NEW_GADGET_CHAIN(SimpleReconstructionProcessor);
		NEW_GADGET_CHAIN(SimpleGRAPPAReconstructionProcessor);
		NEW_GADGET(IsmrmrdAcqMsgReader);
		NEW_GADGET(IsmrmrdAcqMsgWriter);
		NEW_GADGET(IsmrmrdImgMsgReader);
		NEW_GADGET(IsmrmrdImgMsgWriter);
		NEW_GADGET(DicomImageMessageWriter);
		NEW_GADGET(NoiseAdjustGadget);
		NEW_GADGET(PCACoilGadget);
		NEW_GADGET(CoilReductionGadget);
		NEW_GADGET(AsymmetricEchoAdjustROGadget);
		NEW_GADGET(RemoveROOversamplingGadget);
		NEW_GADGET(AcquisitionAccumulateTriggerGadget);
		NEW_GADGET(BucketToBufferGadget);
		NEW_GADGET(GenericReconCartesianReferencePrepGadget);
		NEW_GADGET(GenericReconEigenChannelGadget);
		NEW_GADGET(GenericReconPartialFourierHandlingFilterGadget);
		NEW_GADGET(GenericReconKSpaceFilteringGadget);
		NEW_GADGET(GenericReconCartesianGrappaGadget);
		NEW_GADGET(SimpleReconGadget);
        NEW_GADGET(GenericReconCartesianFFTGadget);
		NEW_GADGET(GenericReconFieldOfViewAdjustmentGadget);
		NEW_GADGET(GenericReconImageArrayScalingGadget);
		NEW_GADGET(FatWaterGadget);
		NEW_GADGET(ImageArraySplitGadget);
		NEW_GADGET(PhysioInterpolationGadget);
		NEW_GADGET(GPURadialSensePrepGadget);
		NEW_GADGET(GPUCGSenseGadget);
		NEW_GADGET(FFTGadget);
		NEW_GADGET(CombineGadget);
		NEW_GADGET(ExtractGadget);
		NEW_GADGET(AutoScaleGadget);
		NEW_GADGET(ComplexToFloatGadget);
		NEW_GADGET(FloatToUShortGadget);
		NEW_GADGET(FloatToShortGadget);
		NEW_GADGET(ImageFinishGadget);
		NEW_GADGET(DicomFinishGadget);
		NEW_GADGET(AcquisitionFinishGadget);
		NEW_GADGET(SimpleReconGadgetSet);
		return unknownObject("object", name, __FILE__, __LINE__);
	}
	CATCH;
}

extern "C"
void* 
cGT_parameter(void* ptr, const char* obj, const char* name)
{
	try {
		if (sirf::iequals(obj, "image"))
			return cGT_imageParameter(ptr, name);
		if (sirf::iequals(obj, "acquisition"))
			return cGT_acquisitionParameter(ptr, name);
		if (sirf::iequals(obj, "acquisitions"))
			return cGT_acquisitionsParameter(ptr, name);
		if (sirf::iequals(obj, "gadget_chain")) {
			GadgetChain& gc = objectFromHandle<GadgetChain>(ptr);
			shared_ptr<aGadget> sptr = gc.gadget_sptr(name);
			if (sptr.get())
				return newObjectHandle(sptr);
			else {
				DataHandle* handle = new DataHandle;
				std::string error = "Gadget ";
				error += name;
				error += " not in the chain";
				ExecutionStatus status(error.c_str(), __FILE__, __LINE__);
				handle->set(0, &status);
				return (void*)handle;
			}
		}
		if (sirf::iequals(obj, "gadget")) {
			aGadget& g = objectFromHandle<aGadget>(ptr);
			std::string value = g.value_of(name);
			return charDataHandleFromCharData(value.c_str());
		}
		if (sirf::iequals(obj, "AcquisitionModel")) {
			return cGT_AcquisitionModelParameter(ptr, name);
		}
		return unknownObject("object", obj, __FILE__, __LINE__);
	}
	CATCH;
}


extern "C"
void*
cGT_setAcquisitionParameter(void* ptr, const char* param_name, const void* val)
{
	CAST_PTR(DataHandle, h_acq, ptr);
	ISMRMRD::Acquisition& acq =
		objectFromHandle<ISMRMRD::Acquisition>(h_acq);

	if (sirf::iequals(param_name, "measurement_uid"))
		acq.measurement_uid() = dataFromHandle<int>(val);
	else if (sirf::iequals(param_name, "scan_counter"))
		acq.scan_counter() = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "acquisition_time_stamp"))
		acq.acquisition_time_stamp() = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "physiology_time_stamp0"))
		acq.physiology_time_stamp()[0] = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "physiology_time_stamp1"))
		acq.physiology_time_stamp()[1] = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "physiology_time_stamp2"))
		acq.physiology_time_stamp()[2] = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "available_channels"))
		acq.available_channels() = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "discard_pre"))
		acq.discard_pre() = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "discard_post"))
		acq.discard_post() = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "center_sample"))
		acq.center_sample() = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "encoding_space_ref"))
		acq.encoding_space_ref() = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_kspace_encode_step_1"))
		acq.idx().kspace_encode_step_1 = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_kspace_encode_step_2"))
		acq.idx().kspace_encode_step_2 = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_average"))
		acq.idx().average = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_slice"))
		acq.idx().slice = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_contrast"))
		acq.idx().contrast = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_phase"))
		acq.idx().phase = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_repetition"))
		acq.idx().repetition = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_set"))
		acq.idx().set = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "idx_segment"))
		acq.idx().segment = dataFromHandle<int>(val);
	else if  (sirf::iequals(param_name, "sample_time_us"))
		acq.sample_time_us() = dataFromHandle<float>(val);
	
	// else if  (sirf::iequals(param_name, "position"))
	// 	acq.position() = dataFromHandle<float*>(val);
	// else if  (sirf::iequals(param_name, "read_dir"))
	// 	acq.read_dir() = dataFromHandle<float*>(val);
	// else if  (sirf::iequals(param_name, "phase_dir"))
	// 	acq.phase_dir() = dataFromHandle<float*>(val);
	// else if  (sirf::iequals(param_name, "slice_dir"))
	// 	acq.slice_dir() = dataFromHandle<float*>(val);
	// else if  (sirf::iequals(param_name, "patient_table_position"))
	// 	acq.patient_table_position() = dataFromHandle<float*>(val);
	else
		return unknownObject("parameter", param_name, __FILE__, __LINE__);

	return new DataHandle;
}

extern "C"
void*
cGT_setParameter(void* ptr, const char* obj, const char* par, const void* val)
{
	try {
		if (sirf::iequals(obj, "coil_sensitivity"))
			return cGT_setCSParameter(ptr, par, val);
		if (sirf::iequals(obj, "acquisition"))
			return cGT_setAcquisitionParameter(ptr,par,val);
		return unknownObject("object", obj, __FILE__, __LINE__);
	}
	CATCH;
}

extern "C"
void*
cGT_CoilSensitivities(const char* file)
{
	try {
		if (std::strlen(file) > 0) {
			shared_ptr<CoilSensitivitiesVector>
                csms(new CoilSensitivitiesVector(file));
			return newObjectHandle<CoilSensitivitiesVector>(csms);
		}
		else {
			shared_ptr<CoilSensitivitiesVector>
                csms(new CoilSensitivitiesVector());
			return newObjectHandle<CoilSensitivitiesVector>(csms);
		}
	}
	CATCH;
}

extern "C"
void*
cGT_setCSParameter(void* ptr, const char* par, const void* val)
{
	CAST_PTR(DataHandle, h_csms, ptr);
	CoilSensitivitiesVector& csms =
		objectFromHandle<CoilSensitivitiesVector>(h_csms);
	if (sirf::iequals(par, "smoothness"))
		csms.set_csm_smoothness(dataFromHandle<int>(val));
	//csms.set_csm_smoothness(intDataFromHandle(val)); // causes problems with Matlab
	else
		return unknownObject("parameter", par, __FILE__, __LINE__);
	return new DataHandle;
}

extern "C"
void*
cGT_computeCoilSensitivities(void* ptr_csms, void* ptr_acqs)
{
	try {
		CAST_PTR(DataHandle, h_csms, ptr_csms);
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		CoilSensitivitiesVector& csms =
			objectFromHandle<CoilSensitivitiesVector>(h_csms);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		csms.calculate(acqs);
		return (void*)new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_computeCoilImages(void* ptr_imgs, void* ptr_acqs)
{
    try {
        CAST_PTR(DataHandle, h_imgs, ptr_imgs);
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        CoilImagesVector& cis =
            objectFromHandle<CoilImagesVector>(h_imgs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);
        cis.calculate(acqs);
        return (void*)new DataHandle;
    }
    CATCH;
}

extern "C"
void*
cGT_computeCoilSensitivitiesFromCoilImages(void* ptr_csms, void* ptr_imgs)
{
    try {
        CAST_PTR(DataHandle, h_csms, ptr_csms);
        CAST_PTR(DataHandle, h_imgs, ptr_imgs);
        CoilSensitivitiesVector& csms =
            objectFromHandle<CoilSensitivitiesVector>(h_csms);
        CoilImagesVector& imgs =
            objectFromHandle<CoilImagesVector>(h_imgs);
        csms.calculate(imgs);
        return (void*)new DataHandle;
    }
    CATCH;
}

extern "C"
void*
cGT_AcquisitionModel(const void* ptr_acqs, const void* ptr_imgs)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		shared_ptr<MRAcquisitionData> sptr_acqs;
		shared_ptr<GadgetronImageData> sptr_imgs;
		getObjectSptrFromHandle<MRAcquisitionData>(h_acqs, sptr_acqs);
		getObjectSptrFromHandle<GadgetronImageData>(h_imgs, sptr_imgs);
		shared_ptr<MRAcquisitionModel> 
			am(new MRAcquisitionModel(sptr_acqs, sptr_imgs));
		return newObjectHandle<MRAcquisitionModel>(am);
	}
	CATCH;
}

extern "C"
void*
cGT_setUpAcquisitionModel
(void* ptr_am, const void* ptr_acqs, const void* ptr_imgs)
{
	try {
		CAST_PTR(DataHandle, h_am, ptr_am);
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
		shared_ptr<MRAcquisitionData> sptr_acqs;
		shared_ptr<GadgetronImageData> sptr_imgs;
		getObjectSptrFromHandle<MRAcquisitionData>(h_acqs, sptr_acqs);
		getObjectSptrFromHandle<GadgetronImageData>(h_imgs, sptr_imgs);
		am.set_up(sptr_acqs, sptr_imgs);
		return (void*)new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_setAcquisitionModelParameter
(void* ptr_am, const char* name, const void* ptr)
{
	try {
		CAST_PTR(DataHandle, h_am, ptr_am);
		if (sirf::iequals(name, "acquisition_template")) {
			CAST_PTR(DataHandle, handle, ptr);
			MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
			shared_ptr<MRAcquisitionData> sptr_acqs;
			getObjectSptrFromHandle<MRAcquisitionData>(handle, sptr_acqs);
			am.set_acquisition_template(sptr_acqs);
		}
		else if (sirf::iequals(name, "image_template")) {
			CAST_PTR(DataHandle, handle, ptr);
			MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
			shared_ptr<GadgetronImageData> sptr_imgs;
			getObjectSptrFromHandle<GadgetronImageData>(handle, sptr_imgs);
			am.set_image_template(sptr_imgs);
		}
		else if (sirf::iequals(name, "coil_sensitivity_maps")) {
			CAST_PTR(DataHandle, handle, ptr);
			MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
			shared_ptr<CoilSensitivitiesVector> sptr_csc;
			getObjectSptrFromHandle<CoilSensitivitiesVector>(handle, sptr_csc);
			am.set_csm(sptr_csc);
		}
		else
			return unknownObject("parameter", name, __FILE__, __LINE__);
		return (void*)new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_AcquisitionModelParameter(void* ptr_am, const char* name)
{
	try {
		CAST_PTR(DataHandle, h_am, ptr_am);
		MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
		if (sirf::iequals(name, "range geometry")) {
			return newObjectHandle(am.acq_template_sptr());
		}
		else if (sirf::iequals(name, "domain geometry")) {
			return newObjectHandle(am.image_template_sptr());
		}
		else
			return unknownObject("parameter", name, __FILE__, __LINE__);
		return (void*)new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_setCSMs(void* ptr_am, const void* ptr_csms)
{
	try {
		CAST_PTR(DataHandle, h_am, ptr_am);
		CAST_PTR(DataHandle, h_csms, ptr_csms);
		MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
		shared_ptr<CoilSensitivitiesVector> sptr_csms;
		getObjectSptrFromHandle<CoilSensitivitiesVector>(h_csms, sptr_csms);
		am.set_csm(sptr_csms);
		return (void*)new DataHandle;
	}
	CATCH;
}

extern "C"
void* cGT_acquisitionModelNorm(void* ptr_am, int num_iter, int verb)
{
	try {
		MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(ptr_am);
		return dataHandle(am.norm(num_iter, verb));
	}
	CATCH;
}

extern "C"
void*
cGT_AcquisitionModelForward(void* ptr_am, const void* ptr_imgs)
{
	try {
		CAST_PTR(DataHandle, h_am, ptr_am);
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(h_imgs);
		shared_ptr<MRAcquisitionData> sptr_acqs = am.fwd(imgs);
		return newObjectHandle<MRAcquisitionData>(sptr_acqs);
	}
	CATCH;
}

extern "C"
void*
cGT_AcquisitionModelBackward(void* ptr_am, const void* ptr_acqs)
{
	try {
		CAST_PTR(DataHandle, h_am, ptr_am);
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionModel& am = objectFromHandle<MRAcquisitionModel>(h_am);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		shared_ptr<GadgetronImageData> sptr_imgs = am.bwd(acqs);
		return newObjectHandle<GadgetronImageData>(sptr_imgs);
	}
	CATCH;
}

extern "C"
void*
cGT_sortAcquisitions(void* ptr_acqs)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		acqs.sort();
		return (void*)new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_sortAcquisitionsByTime(void* ptr_acqs)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		acqs.sort_by_time();

		return (void*)new DataHandle;
	}
	CATCH;
}


extern "C"
void*
cGT_ISMRMRDAcquisitionsFromFile(const char* file, int all)
{
	if (!file_exists(file))
		return fileNotFound(file, __FILE__, __LINE__);
	try {
		shared_ptr<MRAcquisitionData>
			acquisitions(new AcquisitionsVector);
		acquisitions->read(file, all);
		return newObjectHandle<MRAcquisitionData>(acquisitions);
	}
	CATCH;
}

extern "C"
void*
cGT_ISMRMRDAcquisitionsFile(const char* file)
{
	try {
		shared_ptr<MRAcquisitionData> 
			acquisitions(new AcquisitionsVector);
		acquisitions->read(file);
		return newObjectHandle<MRAcquisitionData>(acquisitions);
	}
	CATCH;
}

extern "C"
void*
cGT_processAcquisitions(void* ptr_proc, void* ptr_input)
{
	try {
		CAST_PTR(DataHandle, h_proc, ptr_proc);
		CAST_PTR(DataHandle, h_input, ptr_input);
		AcquisitionsProcessor& proc =
			objectFromHandle<AcquisitionsProcessor>(h_proc);
		MRAcquisitionData& input =
			objectFromHandle<MRAcquisitionData>(h_input);
		proc.process(input);
		shared_ptr<MRAcquisitionData> sptr_ac = proc.get_output();
		return newObjectHandle<MRAcquisitionData>(sptr_ac);
	}
	CATCH;
}

extern "C"
void*
cGT_createEmptyAcquisitionData(void* ptr_ad)
{
	try {
		MRAcquisitionData& ad =
			objectFromHandle<MRAcquisitionData>(ptr_ad);
		shared_ptr<MRAcquisitionData> sptr_ac = ad.new_acquisitions_container();
		return newObjectHandle<MRAcquisitionData>(sptr_ac);
	}
	CATCH;
}

extern "C"
void*
cGT_getAcquisitionsSubset(void* ptr_acqs, size_t const ptr_idx, size_t const num_elem_subset)
{
    try {
        MRAcquisitionData& ad =
            objectFromHandle<MRAcquisitionData>(ptr_acqs);
        shared_ptr<MRAcquisitionData> sptr_subset = ad.new_acquisitions_container();
        int* idx = (int*)ptr_idx;

        std::vector<int> vec_idx(num_elem_subset);
        for(size_t i=0; i<num_elem_subset; ++i)
	    vec_idx.at(i) = *(idx+i);
			
        ad.get_subset(*sptr_subset.get(), vec_idx);
        sptr_subset->sort();

        return newObjectHandle<MRAcquisitionData>(sptr_subset);
    }
    CATCH;
}

extern "C"
void*
cGT_cloneAcquisitions(void* ptr_input)
{
	try {
		CAST_PTR(DataHandle, h_input, ptr_input);
		MRAcquisitionData& input =
			objectFromHandle<MRAcquisitionData>(h_input);
		shared_ptr<MRAcquisitionData> sptr_ac = input.clone();
		return newObjectHandle<MRAcquisitionData>(sptr_ac);
	}
	CATCH;
}

extern "C"
void*
cGT_acquisitionFromContainer(void* ptr_acqs, unsigned int acq_num)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		shared_ptr<ISMRMRD::Acquisition> sptr_acq = acqs.get_acquisition_sptr(acq_num);
		//shared_ptr<ISMRMRD::Acquisition>
		//	sptr_acq(new ISMRMRD::Acquisition);
		//acqs.get_acquisition(acq_num, *sptr_acq);
		return newObjectHandle<ISMRMRD::Acquisition>(sptr_acq);
	}
	CATCH;
}

extern "C"
void*
cGT_appendAcquisition(void* ptr_acqs, void* ptr_acq)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		ISMRMRD::Acquisition& acq = 
			objectFromHandle<ISMRMRD::Acquisition>(ptr_acq);
		acqs.append_acquisition(acq);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_getAcquisitionDataDimensions(void* ptr_acqs, size_t ptr_dim)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		shared_ptr<ISMRMRD::Acquisition>
			sptr_acq(new ISMRMRD::Acquisition);
		int num_reg_dim = acqs.get_acquisitions_dimensions(ptr_dim);
		return dataHandle(num_reg_dim);
	}
	CATCH;
}

extern "C"
void*
cGT_acquisitionDataAsArray(void* ptr_acqs, size_t ptr_z, int all)
{
	try {
		complex_float_t* z = (complex_float_t*)ptr_z;
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		acqs.get_data(z, all);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_fillAcquisitionData(void* ptr_acqs, size_t ptr_z, int all)
{
	complex_float_t* z = (complex_float_t*)ptr_z;
	CAST_PTR(DataHandle, h_acqs, ptr_acqs);
	MRAcquisitionData& acqs =
		objectFromHandle<MRAcquisitionData>(h_acqs);
	acqs.set_data(z, all);
	return new DataHandle;
}

extern "C"
void*
cGT_fillAcquisitionDataFromAcquisitionData(void* ptr_dst, void* ptr_src)
{
	CAST_PTR(DataHandle, h_dst, ptr_dst);
	CAST_PTR(DataHandle, h_src, ptr_src);
	MRAcquisitionData& dst =
		objectFromHandle<MRAcquisitionData>(h_dst);
	MRAcquisitionData& src =
		objectFromHandle<MRAcquisitionData>(h_src);
	dst.copy_acquisitions_data(src);
	return new DataHandle;
}

extern "C"
void*
cGT_acquisitionParameter(void* ptr_acq, const char* name)
{
	CAST_PTR(DataHandle, h_acq, ptr_acq);
	ISMRMRD::Acquisition& acq =
		objectFromHandle<ISMRMRD::Acquisition>(h_acq);
	if (sirf::iequals(name, "version"))
		return dataHandle((int)acq.version());
	if (sirf::iequals(name, "flags"))
		return dataHandle((int)acq.flags());
	if (sirf::iequals(name, "measurement_uid"))
		return dataHandle((int)acq.measurement_uid());
	if (sirf::iequals(name, "scan_counter"))
		return dataHandle((int)acq.scan_counter());
	if (sirf::iequals(name, "acquisition_time_stamp"))
		return dataHandle((int)acq.acquisition_time_stamp());
	if (sirf::iequals(name, "number_of_samples"))
		return dataHandle((int)acq.number_of_samples());
	if (sirf::iequals(name, "available_channels"))
		return dataHandle((int)acq.available_channels());
	if (sirf::iequals(name, "active_channels"))
		return dataHandle((int)acq.active_channels());
	if (sirf::iequals(name, "discard_pre"))
		return dataHandle((int)acq.discard_pre());
	if (sirf::iequals(name, "discard_post"))
		return dataHandle((int)acq.discard_post());
	if (sirf::iequals(name, "center_sample"))
		return dataHandle((int)acq.center_sample());
	if (sirf::iequals(name, "encoding_space_ref"))
		return dataHandle((int)acq.encoding_space_ref());
	if (sirf::iequals(name, "trajectory_dimensions"))
		return dataHandle((int)acq.trajectory_dimensions());
	if (sirf::iequals(name, "idx_kspace_encode_step_1"))
		return dataHandle((int)acq.idx().kspace_encode_step_1);
	if (sirf::iequals(name, "idx_kspace_encode_step_2"))
		return dataHandle((int)acq.idx().kspace_encode_step_2);
	if (sirf::iequals(name, "idx_average"))
		return dataHandle((int)acq.idx().average);
	if (sirf::iequals(name, "idx_slice"))
		return dataHandle((int)acq.idx().slice);
	if (sirf::iequals(name, "idx_contrast"))
		return dataHandle((int)acq.idx().contrast);
	if (sirf::iequals(name, "idx_phase"))
		return dataHandle((int)acq.idx().phase);
	if (sirf::iequals(name, "idx_repetition"))
		return dataHandle((int)acq.idx().repetition);
	if (sirf::iequals(name, "idx_set"))
		return dataHandle((int)acq.idx().set);
	if (sirf::iequals(name, "idx_segment"))
		return dataHandle((int)acq.idx().segment);
	if (sirf::iequals(name, "physiology_time_stamp"))
		return dataHandle(acq.physiology_time_stamp());
	if (sirf::iequals(name, "channel_mask"))
		return dataHandle(acq.channel_mask());
	if (sirf::iequals(name, "sample_time_us"))
		return dataHandle((float)acq.sample_time_us());
	if (sirf::iequals(name, "position"))
		return dataHandle((float*)acq.position());
	if (sirf::iequals(name, "read_dir"))
		return dataHandle((float*)acq.read_dir());
	if (sirf::iequals(name, "phase_dir"))
		return dataHandle((float*)acq.phase_dir());
	if (sirf::iequals(name, "slice_dir"))
		return dataHandle((float*)acq.slice_dir());
	if (sirf::iequals(name, "patient_table_position"))
		return dataHandle((float*)acq.patient_table_position());
	return parameterNotFound(name, __FILE__, __LINE__);
}

extern "C"
void*
cGT_acquisitionsParameter(void* ptr_acqs, const char* name)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		if (sirf::iequals(name, "undersampled"))
			return dataHandle((int)acqs.undersampled());
		if (sirf::iequals(name, "sorted"))
			return dataHandle((int)acqs.sorted());
		if (sirf::iequals(name, "info"))
			return charDataHandleFromCharData(acqs.acquisitions_info().c_str());
		return parameterNotFound(name, __FILE__, __LINE__);
	}
	CATCH;
}

extern "C"
void*
cGT_acquisitionParameterInfo(void* ptr_acqs, const char* name,
	int* info)
{
	try {
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(ptr_acqs);
		acqs.ismrmrd_par_info(name, info);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_acquisitionParameterValuesInt(void* ptr_acqs, const char* name,
	int from, int till, int n, unsigned long long int* values)
{
	try {
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(ptr_acqs);
		int na = acqs.number();
		if (na < 1)
			return new DataHandle;
		if (till < 0)
			till = na;
		for (int a = from; a < till; a++) {
			ISMRMRD::Acquisition acq;
			acqs.get_acquisition(a, acq);
			acqs.ismrmrd_par_value(acq, name, values);
			values += n;
		}
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_acquisitionParameterValuesFloat(void* ptr_acqs, const char* name,
	int from, int till, int n, float* values)
{
	try {
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(ptr_acqs);
		int na = acqs.number();
		if (na < 1)
			return new DataHandle;
		if (till < 0)
			till = na;
		for (int a = from; a < till; a++) {
			ISMRMRD::Acquisition acq;
			acqs.get_acquisition(a, acq);
			acqs.ismrmrd_par_value(acq, name, values);
			values += n;
		}
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_setAcquisitionsInfo(void* ptr_acqs, const char* info)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		acqs.set_acquisitions_info(info);
		return new DataHandle;
	}
	CATCH;

}

extern "C"
void*
cGT_setGRPETrajectory(void* ptr_acqs)
{
    try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);

        GRPETrajectoryPrep rpe_prep;
        rpe_prep.set_trajectory(acqs);

        return new DataHandle;
    }
    CATCH;

}


extern "C"
void*
cGT_setRadial2DTrajectory(void* ptr_acqs)
{
    try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);

        Radial2DTrajprep radial2D_prep;
        radial2D_prep.set_trajectory(acqs);

        return new DataHandle;
    }
    CATCH;
}


extern "C"
void*
cGT_setGoldenAngle2DTrajectory(void* ptr_acqs)
{
    try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);

        GoldenAngle2DTrajprep ga2D_prep;
        ga2D_prep.set_trajectory(acqs);

        return new DataHandle;
    }
    CATCH;
}

extern "C"
void*
cGT_getDataTrajectory(void* ptr_acqs, size_t ptr_traj)
{
    try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);

        float* fltptr_traj = (float*) ptr_traj;
		
		if(acqs.get_trajectory_type() == ISMRMRD::TrajectoryType::CARTESIAN)
		{
			auto traj = sirf::CartesianTrajectoryPrep::get_trajectory(acqs);
			memcpy(fltptr_traj,&(*traj.begin()), traj.size()*sizeof(TrajectoryPreparation2D::TrajPointType));
		}
    	else if(acqs.get_trajectory_type() == ISMRMRD::TrajectoryType::OTHER)
		{
			sirf::GRPETrajectoryPrep tp;
			auto traj = tp.get_trajectory(acqs);
			memcpy(fltptr_traj,&(*traj.begin()), traj.size()*sizeof(GRPETrajectoryPrep::TrajPointType));
		}
		else if(acqs.get_trajectory_type() == ISMRMRD::TrajectoryType::RADIAL)
		{
			sirf::Radial2DTrajprep tp;
			auto traj = tp.get_trajectory(acqs);
			memcpy(fltptr_traj,&(*traj.begin()), traj.size()*sizeof(Radial2DTrajprep::TrajPointType));
		}
		else if(acqs.get_trajectory_type() == ISMRMRD::TrajectoryType::GOLDENANGLE)
		{
			sirf::GoldenAngle2DTrajprep tp;
			auto traj = tp.get_trajectory(acqs);
			memcpy(fltptr_traj,&(*traj.begin()), traj.size()*sizeof(GoldenAngle2DTrajprep::TrajPointType));
		}
	
        return new DataHandle;
    }
    CATCH;
}

extern "C"
void*
cGT_setDataTrajectory(void* ptr_acqs, int const traj_dim, size_t ptr_traj)
{
    try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);

        float* fltptr_traj = (float*) ptr_traj;
		acqs.set_trajectory(traj_dim, fltptr_traj);
	
        return new DataHandle;
    }
    CATCH;
}

extern "C"
void* 
cGT_setEncodingLimits(void* ptr_acqs, const char* name, const int min, const int max, const int ctr)
{
	try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);

		std::string name_as_string(name);
		std::tuple<unsigned short, unsigned short, unsigned short> limit{min, max, ctr};
		acqs.set_encoding_limits(name_as_string, limit);

        return new DataHandle;
    }
    CATCH;
}

extern "C"
void*
cGT_setTrajectoryType(void* ptr_acqs, int const traj_type)
{
    try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);
		
		const ISMRMRD::TrajectoryType type = static_cast<ISMRMRD::TrajectoryType>(traj_type);
		acqs.set_trajectory_type(type);
			
        return new DataHandle;
    }
    CATCH;
}

extern "C"
void* cGT_setAcquisitionUserFloat(void* ptr_acqs, size_t ptr_floats, int idx)
{
    try {
        CAST_PTR(DataHandle, h_acqs, ptr_acqs);
        MRAcquisitionData& acqs =
            objectFromHandle<MRAcquisitionData>(h_acqs);

        float* user_data = (float*) ptr_floats;
        acqs.set_user_floats(user_data, idx);

        return new DataHandle;
    }
    CATCH;
}

extern "C"
void*
cGT_imageParameter(void* ptr_im, const char* name)
{
	try {
		ImageWrap& im = objectFromHandle<ImageWrap>(ptr_im);
		ISMRMRD::ImageHeader& head = im.head();
		if (sirf::iequals(name, "version"))
			return dataHandle((int)head.version);
		if (sirf::iequals(name, "flags"))
			return dataHandle((int)head.flags);
		if (sirf::iequals(name, "data_type"))
			return dataHandle((int)head.data_type);
		if (sirf::iequals(name, "measurement_uid"))
			return dataHandle((int)head.measurement_uid);
		if (sirf::iequals(name, "channels"))
			return dataHandle((int)head.channels);
		if (sirf::iequals(name, "average"))
			return dataHandle((int)head.average);
		if (sirf::iequals(name, "slice"))
			return dataHandle((int)head.slice);
		if (sirf::iequals(name, "contrast"))
			return dataHandle((int)head.contrast);
		if (sirf::iequals(name, "phase"))
			return dataHandle((int)head.phase);
		if (sirf::iequals(name, "repetition"))
			return dataHandle((int)head.repetition);
		if (sirf::iequals(name, "set"))
			return dataHandle((int)head.set);
		if (sirf::iequals(name, "acquisition_time_stamp"))
			return dataHandle((int)head.acquisition_time_stamp);
		if (sirf::iequals(name, "image_type"))
			return dataHandle((int)head.image_type);
		if (sirf::iequals(name, "image_index"))
			return dataHandle((int)head.image_index);
		if (sirf::iequals(name, "image_series_index"))
			return dataHandle((int)head.image_series_index);
		if (sirf::iequals(name, "attribute_string_len"))
			return dataHandle((int)head.attribute_string_len);
		if (sirf::iequals(name, "matrix_size"))
			return dataHandle(head.matrix_size);
		if (sirf::iequals(name, "physiology_time_stamp"))
			return dataHandle(head.physiology_time_stamp);
		if (sirf::iequals(name, "field_of_view"))
			return dataHandle((float*)head.field_of_view);
		if (sirf::iequals(name, "position"))
			return dataHandle((float*)head.position);
		if (sirf::iequals(name, "read_dir"))
			return dataHandle((float*)head.read_dir);
		if (sirf::iequals(name, "phase_dir"))
			return dataHandle((float*)head.phase_dir);
		if (sirf::iequals(name, "slice_dir"))
			return dataHandle((float*)head.slice_dir);
		if (sirf::iequals(name, "patient_table_position"))
			return dataHandle((float*)head.patient_table_position);
		return parameterNotFound(name, __FILE__, __LINE__);
	}
	CATCH;
}

extern "C"
void*
cGT_reconstructImages(void* ptr_recon, void* ptr_input, const char* dcm_prefix)
{
	try {
		CAST_PTR(DataHandle, h_recon, ptr_recon);
		CAST_PTR(DataHandle, h_input, ptr_input);
		ImagesReconstructor& recon = objectFromHandle<ImagesReconstructor>(h_recon);
		MRAcquisitionData& input = objectFromHandle<MRAcquisitionData>(h_input);
		recon.set_dcm_prefix(dcm_prefix);
		recon.process(input);
		return new DataHandle;
	}
	CATCH;

}

extern "C"
void*
cGT_reconstructedImages(void* ptr_recon)
{
	try {
		CAST_PTR(DataHandle, h_recon, ptr_recon);
		ImagesReconstructor& recon = objectFromHandle<ImagesReconstructor>(h_recon);
		shared_ptr<GadgetronImageData> sptr_img = recon.get_output();
		return newObjectHandle<GadgetronImageData>(sptr_img);
	}
	CATCH;

}

extern "C"
void*
cGT_readImages(const char* file)
{
	if (!file_exists(file))
		return fileNotFound(file, __FILE__, __LINE__);
	try {
		shared_ptr<GadgetronImageData> sptr_img(new GadgetronImagesVector);
		sptr_img->read(file);
		return newObjectHandle<GadgetronImageData>(sptr_img);
	}
	CATCH;
}

extern "C"
void *
cGT_ImageFromAcquisitiondata(void* ptr_acqs)
{
	try {
		CAST_PTR(DataHandle, h_acqs, ptr_acqs);
		MRAcquisitionData& acqs =
			objectFromHandle<MRAcquisitionData>(h_acqs);
		auto sptr_iv = std::make_shared<GadgetronImagesVector>(acqs);
		return newObjectHandle<GadgetronImageData>(sptr_iv);
	}
	CATCH;
}

extern "C"
void*
cGT_processImages(void* ptr_proc, void* ptr_input)
{
	try {
		CAST_PTR(DataHandle, h_proc, ptr_proc);
		CAST_PTR(DataHandle, h_input, ptr_input);
		ImagesProcessor& proc = objectFromHandle<ImagesProcessor>(h_proc);
		GadgetronImageData& input = objectFromHandle<GadgetronImageData>(h_input);
		proc.process(input);
		shared_ptr<GadgetronImageData> sptr_img = proc.get_output();
		return newObjectHandle<GadgetronImageData>(sptr_img);
	}
	CATCH;

}

extern "C"
void*
cGT_selectImages(void* ptr_input, const char* attr, const char* target)
{
	try {
		CAST_PTR(DataHandle, h_input, ptr_input);
		GadgetronImageData& input = objectFromHandle<GadgetronImageData>(h_input);
		shared_ptr<GadgetronImageData> sptr_img = input.clone(attr, target);
		return newObjectHandle<GadgetronImageData>(sptr_img);
	}
	CATCH;
}

extern "C"
void*
cGT_writeImages(void* ptr_imgs, const char* filename, const char* ext)
{
	try {
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(h_imgs);
        // If .h5
		if (strcmp(ext, "h5") == 0) {
			std::string fullname(filename);
			fullname += ".";
			fullname += ext;
			imgs.write(fullname);
		}
        // Else if dicom
		else if (strcmp(ext, "dcm") == 0) {
            imgs.write(filename,"",true);
		}
        else
            throw std::runtime_error("cGT_writeImages: Unknown extension");
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_imageWrapFromContainer(void* ptr_imgs, unsigned int img_num)
{
	CAST_PTR(DataHandle, h_imgs, ptr_imgs);
	GadgetronImageData& images = objectFromHandle<GadgetronImageData>(h_imgs);
	return newObjectHandle<ImageWrap>(images.sptr_image_wrap(img_num));
}

extern "C"
void
cGT_getImageDim(void* ptr_img, size_t ptr_dim)
{
	int* dim = (int*)ptr_dim;
	ImageWrap& image = objectFromHandle<ImageWrap>(ptr_img);
	image.get_dim(dim);
//	image.show_attributes();
}

extern "C"
void*
cGT_imageType(const void* ptr_img)
{
	try {
		ImageWrap& image = objectFromHandle<ImageWrap>(ptr_img);
		return dataHandle(image.type());
	}
	CATCH;
}

extern "C"
void*
cGT_setImageType(const void* ptr_img, int image_type)
{
	try {
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(ptr_img);
		imgs.set_image_type(image_type);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_getImageDataAsFloatArray(void* ptr_imgs, size_t ptr_data)
{
	try {
		float* data = (float*)ptr_data;
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(h_imgs);
		imgs.get_real_data(data);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_setImageDataFromFloatArray(void* ptr_imgs, size_t ptr_data)
{
	try {
		float* data = (float*)ptr_data;
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(h_imgs);
		imgs.set_real_data(data);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_getImageDataAsCmplxArray(void* ptr_imgs, size_t ptr_z)
{
	try {
		complex_float_t* z = (complex_float_t*)ptr_z;
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(h_imgs);
		imgs.get_data(z);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_setImageDataFromCmplxArray(void* ptr_imgs, size_t ptr_z)
{
	try {
		complex_float_t* z = (complex_float_t*)ptr_z;
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(h_imgs);
		imgs.set_data(z);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_realImageData(void* ptr_imgs, const char* way)
{
	try {
		CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		GadgetronImageData& imgs = objectFromHandle<GadgetronImageData>(h_imgs);
		if (sirf::iequals(way, "real"))
			return newObjectHandle<GadgetronImageData>(imgs.real());
		else
			return newObjectHandle<GadgetronImageData>(imgs.abs());
	}
	CATCH;
}

extern "C"
void* cGT_print_header(const void* ptr_imgs, const int im_idx)
{
    try {
        CAST_PTR(DataHandle, h_imgs, ptr_imgs);
		GadgetronImagesVector& imgs = objectFromHandle<GadgetronImagesVector>(h_imgs);
        imgs.print_header(im_idx);
		return new DataHandle;
	}
	CATCH;
}

extern "C"
void*
cGT_imageDataType(const void* ptr_x, int im_num)
{
	try {
		CAST_PTR(DataHandle, h_x, ptr_x);
		GadgetronImageData& x = objectFromHandle<GadgetronImageData>(h_x);
		return dataHandle(x.image_data_type(im_num));
	}
	CATCH;
}

extern "C"
void*
cGT_setConnectionTimeout(void* ptr_con, unsigned int timeout_ms)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		con.set_timeout(timeout_ms);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_setHost(void* ptr_gc, const char* host)
{
	try {
		CAST_PTR(DataHandle, h_gc, ptr_gc);
		GadgetChain& gc = objectFromHandle<GadgetChain>(h_gc);
		gc.set_host(host);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_setPort(void* ptr_gc, const char* port)
{
	try {
		CAST_PTR(DataHandle, h_gc, ptr_gc);
		GadgetChain& gc = objectFromHandle<GadgetChain>(h_gc);
		gc.set_port(port);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_addReader(void* ptr_gc, const char* id, const void* ptr_r)
{
	try {
		CAST_PTR(DataHandle, h_gc, ptr_gc);
		CAST_PTR(DataHandle, h_r, ptr_r);
		GadgetChain& gc = objectFromHandle<GadgetChain>(h_gc);
		shared_ptr<aGadget> sptr_g;
		getObjectSptrFromHandle<aGadget>(h_r, sptr_g);
		gc.add_reader(id, sptr_g);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_addWriter(void* ptr_gc, const char* id, const void* ptr_w)
{
	try {
		CAST_PTR(DataHandle, h_gc, ptr_gc);
		CAST_PTR(DataHandle, h_w, ptr_w);
		GadgetChain& gc = objectFromHandle<GadgetChain>(h_gc);
		shared_ptr<aGadget> sptr_g;
		getObjectSptrFromHandle<aGadget>(h_w, sptr_g);
		gc.add_writer(id, sptr_g);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_addGadget(void* ptr_gc, const char* id, const void* ptr_g)
{
	try {
		CAST_PTR(DataHandle, h_gc, ptr_gc);
		CAST_PTR(DataHandle, h_g, ptr_g);
		GadgetChain& gc = objectFromHandle<GadgetChain>(h_gc);
		shared_ptr<aGadget> sptr_g;
		getObjectSptrFromHandle<aGadget>(h_g, sptr_g);
		gc.add_gadget(id, sptr_g);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_setGadgetProperty(void* ptr_g, const char* prop, const char* value)
{
	try {
		CAST_PTR(DataHandle, h_g, ptr_g);
		aGadget& g = objectFromHandle<aGadget>(h_g);
		//std::cout << g.name() << std::endl;
		g.set_property(prop, value);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_setGadgetProperties(void* ptr_g, const char* props)
{
	try {
		CAST_PTR(DataHandle, h_g, ptr_g);
		aGadget& g = objectFromHandle<aGadget>(h_g);
		std::string in(props);
		std::string prop;
		std::string value;
		size_t n = in.length();
		size_t i, j;
		i = 0;
		for (;;) {
			j = in.find_first_not_of(" \t\n\v\f\r", i);
			if (j == std::string::npos)
				break;
			i = j;
			j = in.find_first_of("= \t\n\v\f\r", i);
			if (j == std::string::npos)
				j = n;
			prop = in.substr(i, j - i);
			//std::cout << prop << '\n';
			i = j;
			i = in.find_first_not_of("= \t\n\v\f\r", i);
			j = in.find_first_of(", \t\n\v\f\r", i);
			if (j == std::string::npos)
				j = n;
			value = in.substr(i, j - i);
			//std::cout << value << '\n';
			g.set_property(prop.c_str(), value.c_str());
			if (j < n && in[j] == ',')
				j++;
			i = j;
		}
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_configGadgetChain(void* ptr_con, void* ptr_gc)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		CAST_PTR(DataHandle, h_gc, ptr_gc);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		GadgetChain& gc = objectFromHandle<GadgetChain>(h_gc);
		std::string config = gc.xml();
		//std::cout << config << std::endl;
		con.send_gadgetron_configuration_script(config);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_registerImagesReceiver(void* ptr_con, void* ptr_img)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		CAST_PTR(DataHandle, h_img, ptr_img);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		shared_ptr<GadgetronImageData> sptr_images;
		getObjectSptrFromHandle<GadgetronImageData>(h_img, sptr_images);
		con.register_reader(GADGET_MESSAGE_ISMRMRD_IMAGE,
			shared_ptr<GadgetronClientMessageReader>
			(new GadgetronClientImageMessageCollector(sptr_images)));
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_connect(void* ptr_con, const char* host, const char* port)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		con.connect(host, port);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_sendConfigScript(void* ptr_con, const char* config)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		con.send_gadgetron_configuration_script(config);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_sendConfigFile(void* ptr_con, const char* file)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		con.send_gadgetron_configuration_file(file);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_sendParameters(void* ptr_con, const void* ptr_par)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		CAST_PTR(DataHandle, h_par, ptr_par);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		std::string& par = objectFromHandle<std::string>(h_par);
		//std::cout << par << std::endl;
		con.send_gadgetron_parameters(par);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_sendParametersString(void* ptr_con, const char* par)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		con.send_gadgetron_parameters(par);
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void* 
cGT_sendAcquisitions(void* ptr_con, void* ptr_dat)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		CAST_PTR(DataHandle, h_dat, ptr_dat);
	
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		Mutex mutex;
		std::mutex& mtx = mutex();
		mtx.lock();
		ISMRMRD::Dataset& ismrmrd_dataset = 
			objectFromHandle<ISMRMRD::Dataset>(h_dat);
		mtx.unlock();
	
		uint32_t acquisitions = 0;
		{
			mtx.lock();
			acquisitions = ismrmrd_dataset.getNumberOfAcquisitions();
			mtx.unlock();
		}

		//std::cout << acquisitions << " acquisitions" << std::endl;
	
		ISMRMRD::Acquisition acq_tmp;
		for (uint32_t i = 0; i < acquisitions; i++) {
			{
				//boost::mutex::scoped_lock scoped_lock(mtx);
				mtx.lock();
				ismrmrd_dataset.readAcquisition(i, acq_tmp);
				mtx.unlock();
			}
			con.send_ismrmrd_acquisition(acq_tmp);
		}
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_sendImages(void* ptr_con, void* ptr_img)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		CAST_PTR(DataHandle, h_img, ptr_img);

		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		GadgetronImageData& images = objectFromHandle<GadgetronImageData>(h_img);
		for (unsigned int i = 0; i < images.number(); i++) {
			ImageWrap& iw = images.image_wrap(i);
			con.send_wrapped_image(iw);
		}
	}
	CATCH;

	return (void*)new DataHandle;
}

extern "C"
void*
cGT_disconnect(void* ptr_con)
{
	try {
		CAST_PTR(DataHandle, h_con, ptr_con);
		GTConnector& conn = objectFromHandle<GTConnector>(h_con);
		GadgetronClientConnector& con = conn();
		con.send_gadgetron_close();
		con.wait();
	}
	CATCH;

	return (void*)new DataHandle;
}

//extern "C"
//void*
//parameter(void* ptr, const char* obj, const char* name)
//{
//	return cGT_parameter(ptr, obj, name);
//}
//
//extern "C"
//void*
//setParameter(void* ptr, const char* obj, const char* par, const void* val)
//{
//	return cGT_setParameter(ptr, obj, par, val);
//}
