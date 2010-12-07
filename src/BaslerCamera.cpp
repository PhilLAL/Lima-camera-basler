#include "BaslerCamera.h"
#include <sstream>
#include <iostream>
#include <string>
#include <math.h>
using namespace lima;
using namespace std;


// Buffers for grabbing
static const uint32_t c_nBuffers 			= 1;


//---------------------------
//- Ctor
//---------------------------
Camera::Camera(const std::string& camera_ip)
		: m_buffer_cb_mgr(m_buffer_alloc_mgr),
		  m_buffer_ctrl_mgr(m_buffer_cb_mgr)
{
	DEB_CONSTRUCTOR();
	cout<<"Camera::Camera() - [BEGIN]"<<endl;
	
	try
    {		
		m_stop_already_done = true;
		m_status = Camera::Ready;		
		Pylon::PylonInitialize( );
        // Create the transport layer object needed to enumerate or
        // create a camera object of type Camera_t::DeviceClass()
		cout<<"Create a camera object of type Camera_t::DeviceClass()"<<endl;
        pTl_ = CTlFactory::GetInstance().CreateTl(Camera_t::DeviceClass());

        // Exit application if the specific transport layer is not available
        if (! pTl_)
        {
            cerr << "Failed to create transport layer!" << endl;
			Pylon::PylonTerminate( );			
            throw LIMA_HW_EXC(Error, "Failed to create transport layer!");
        }

        // Get all attached cameras and exit application if no camera is found
		cout<<"Get all attached cameras, EnumerateDevices"<<endl;
        if (0 == pTl_->EnumerateDevices(devices_))
        {
            cerr << "No camera present!" << endl;
			Pylon::PylonTerminate( );			
            throw LIMA_HW_EXC(Error, "No camera present!");
        }

		// Find the device with an IP corresponding to the one given in property
		Pylon::DeviceInfoList_t::const_iterator it;		
		
		// camera_ip is not really necessarily an IP, it may also be a DNS name
		// pylon_camera_ip IS an IP
		Pylon::String_t pylon_camera_ip(yat::Address(camera_ip, 0).get_ip_address().c_str());
		
		for (it = devices_.begin(); it != devices_.end(); it++)
		{
			const Camera_t::DeviceInfo_t& gige_device_info = static_cast<const Camera_t::DeviceInfo_t&>(*it);
			Pylon::String_t current_ip = gige_device_info.GetIpAddress();
			if (current_ip == pylon_camera_ip)
				break;
		}

		if (it == devices_.end())
		{
			cerr << "Camera " << camera_ip<< " not found."<<endl;
			Pylon::PylonTerminate( );
			throw LIMA_HW_EXC(Error, "Camera not found!");
		}
		
		// Create the camera object of the first available camera
		// The camera object is used to set and get all available
		// camera features.
		cout <<"Create the camera object attached to ip address : "<<camera_ip<<endl;
		Camera_ = new Camera_t(pTl_->CreateDevice(*it));
		
		if( !Camera_->GetDevice() )
		{
			cerr << "Unable to get the camera from transport_layer."<<endl;
			Pylon::PylonTerminate( );
			throw LIMA_HW_EXC(Error, "Unable to get the camera from transport_layer!");
		}
	  
        
        // Open the camera
        Camera_->Open();

        // Get the first stream grabber object of the selected camera
		cout<<"Get the first stream grabber object of the selected camera"<<endl;
        StreamGrabber_ = new Camera_t::StreamGrabber_t(Camera_->GetStreamGrabber(0));

        // Open the stream grabber
		cout<<"Open the stream grabber"<<endl;
        StreamGrabber_->Open();

        // Set the image format and AOI
		cout<<"Set the image format and AOI"<<endl;		
        Camera_->PixelFormat.SetValue(PixelFormat_Mono16);
        Camera_->OffsetX.SetValue(0);
        Camera_->OffsetY.SetValue(0);
        Camera_->Width.SetValue(Camera_->Width.GetMax());
        Camera_->Height.SetValue(Camera_->Height.GetMax());

        // Set the camera to continuous frame mode
		cout<<"Set the camera to continuous frame mode"<<endl;
        Camera_->TriggerSelector.SetValue(TriggerSelector_AcquisitionStart);
        Camera_->TriggerMode.SetValue(TriggerMode_Off);
        Camera_->AcquisitionMode.SetValue(AcquisitionMode_Continuous);
        Camera_->ExposureMode.SetValue(ExposureMode_Timed);

        // Get the image buffer size
		cout<<"Get the image buffer size"<<endl;
		ImageSize_ = (size_t)(Camera_->PayloadSize.GetValue());
       
        // We won't use image buffers greater than ImageSize
		cout<<"We won't use image buffers greater than ImageSize"<<endl;		
        StreamGrabber_->MaxBufferSize.SetValue((const size_t)ImageSize_);

        // We won't queue more than c_nBuffers image buffers at a time
		cout<<"We won't queue more than c_nBuffers image buffers at a time"<<endl;
        StreamGrabber_->MaxNumBuffer.SetValue(c_nBuffers);

	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription()<< endl;
		Pylon::PylonTerminate( );
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }	
	cout<<"Camera::Camera() - [END]"<<endl;	
}

//---------------------------
//- Dtor
//---------------------------
Camera::~Camera()
{
	DEB_DESTRUCTOR();
	cout<<"Camera::~Camera() - [BEGIN]"<<endl;	
	try
	{
		// Close stream grabber
		cout<<"Close stream grabber"<<endl;
		StreamGrabber_->Close();
	
		// Close camera
		cout<<"Close camera"<<endl;
		Camera_->Close();
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
		Pylon::PylonTerminate( );
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }
	delete pTl_;
	delete Camera_;
	delete StreamGrabber_;	
    Pylon::PylonTerminate( );
	cout<<"Camera::~Camera() - [END]"<<endl;		
}

//---------------------------
//- Camera::start()
//---------------------------
void Camera::start()
{
	DEB_MEMBER_FUNCT();
	cout<<"Camera::start() - [BEGIN]"<<endl;
	
    try
    {
		m_image_number=0;		
		// Allocate all resources for grabbing. Critical parameters like image
		// size now must not be changed until FinishGrab() is called.
		cout<<"Allocate all resources for grabbing, PrepareGrab"<<endl;
		StreamGrabber_->PrepareGrab();
	
		// Buffers used for grabbing must be registered at the stream grabber.
		// The registration returns a handle to be used for queuing the buffer.
		cout<<"Buffers used for grabbing must be registered at the stream grabber"<<endl;
		for (uint32_t i = 0; i < c_nBuffers; ++i)
		{
			CGrabBuffer *pGrabBuffer = new CGrabBuffer((const size_t)ImageSize_);
			pGrabBuffer->SetBufferHandle(StreamGrabber_->RegisterBuffer(pGrabBuffer->GetBufferPointer(), (const size_t)ImageSize_));
	
			// Put the grab buffer object into the buffer list
			cout<<"Put the grab buffer object into the buffer list"<<endl;
			BufferList_.push_back(pGrabBuffer);
		}
	
		cout<<"Handle to be used for queuing the buffer"<<endl;
		for (vector<CGrabBuffer*>::const_iterator x = BufferList_.begin(); x != BufferList_.end(); ++x)
		{
			// Put buffer into the grab queue for grabbing
			cout<<"Put buffer into the grab queue for grabbing"<<endl;
			StreamGrabber_->QueueBuffer((*x)->GetBufferHandle(), NULL);
		}
		
		// Let the camera acquire images continuously ( Acquisiton mode equals Continuous! )
		m_status = Camera::Exposure;
		cout<<"Let the camera acquire images continuously"<<endl;	
		Camera_->AcquisitionStart.Execute();
		this->post(new yat::Message(DLL_START_MSG), kPOST_MSG_TMO);
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }
	cout<<"Camera::start() - [END]"<<endl;
}

//---------------------------
//- Camera::stop()
//---------------------------
void Camera::stop()
{
	DEB_MEMBER_FUNCT();
	cout<<"Camera::stop() - [BEGIN]"<<endl;
	{	yat::MutexLock scoped_lock(lock_);	
		this->post(new yat::Message(DLL_STOP_MSG), kPOST_MSG_TMO);
	}
	cout<<"Camera::stop() - [END]"<<endl;
}
//---------------------------
//- Camera::FreeImage()
//---------------------------
void Camera::FreeImage()
{
	try
	{
		m_image_number = 0;
		m_status = Camera::Ready;
		if(!m_stop_already_done)
		{
			m_stop_already_done = true;
			// Get the pending buffer back (You are not allowed to deregister
			// buffers when they are still queued)
			StreamGrabber_->CancelGrab();
	
			// Get all buffers back
			for (GrabResult r; StreamGrabber_->RetrieveResult(r););
			
			// Stop acquisition
			cout<<"Stop acquisition"<<endl;
			Camera_->AcquisitionStop.Execute();
			
			// Clean up
		
			// You must deregister the buffers before freeing the memory
			cout<<"Must deregister the buffers before freeing the memory"<<endl;
			for (vector<CGrabBuffer*>::iterator it = BufferList_.begin(); it != BufferList_.end(); it++)
			{
				StreamGrabber_->DeregisterBuffer((*it)->GetBufferHandle());
				delete *it;
				*it = NULL;
			}
			BufferList_.clear();
			// Free all resources used for grabbing
			cout<<"Free all resources used for grabbing"<<endl;
			StreamGrabber_->FinishGrab();
		}
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }	
}
		
//---------------------------
//- Camera::GetImage()
//---------------------------
void Camera::GetImage()
{
	try
	{
		if(m_stop_already_done)
			return;		
		StdBufferCbMgr& buffer_mgr = m_buffer_cb_mgr;
		m_buffer_ctrl_mgr.setStartTimestamp(Timestamp::now());
		// Wait for the grabbed image with timeout of 10 seconds (10 s is the max of exposure time fixed by pylon))
		if (StreamGrabber_->GetWaitObject().Wait(10000))
		{
			// Get the grab result from the grabber's result queue
			GrabResult Result;
			StreamGrabber_->RetrieveResult(Result);

			if (Grabbed == Result.Status())
			{
				// Grabbing was successful, process image
				m_status = Camera::Readout;
				cout << "image#" <<m_image_number <<" acquired !" << endl;							
				int buffer_nb, concat_frame_nb;
				buffer_mgr.setStartTimestamp(Timestamp::now());
				buffer_mgr.acqFrameNb2BufferNb(m_image_number, buffer_nb, concat_frame_nb);
				void *ptr = buffer_mgr.getBufferPtr(buffer_nb,   concat_frame_nb);
				memcpy((uint16_t *)ptr,(uint16_t *)( Result.Buffer()),Camera_->Width.GetMax()*Camera_->Height.GetMax()*2);
				HwFrameInfoType frame_info;
				frame_info.acq_frame_nb = m_image_number;
				buffer_mgr.newFrameReady(frame_info);
	
				// Reuse the buffer for grabbing the next image
				if (m_image_number < m_nb_frames - c_nBuffers)
					StreamGrabber_->QueueBuffer(Result.Handle(), NULL);
				m_image_number++;
			}
			else if (Failed == Result.Status())
			{
				// Error handling
				cerr << "No image acquired!" << endl;
				cerr << "Error code : 0x" << hex << Result.GetErrorCode() << endl;
				cerr << "Error description : "   << Result.GetErrorDescription() << endl;

				// Reuse the buffer for grabbing the next image
				if (m_image_number < m_nb_frames - c_nBuffers)
					StreamGrabber_->QueueBuffer(Result.Handle(), NULL);
			}
			
			{
				yat::MutexLock scoped_lock(lock_);
				if (m_image_number<m_nb_frames)
				{
					this->post(new yat::Message(DLL_GET_IMAGE_MSG), kPOST_MSG_TMO);	
				}
			}
		}
		else
		{
			// Timeout
			cerr << "Timeout occurred!" << endl;
			this->stop();
		}
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }			
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getImageSize(Size& size)
{
	DEB_MEMBER_FUNCT();
	try
	{
		size= Size(Camera_->Width.GetMax(),Camera_->Height.GetMax());
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }			
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getPixelSize(double& size)
{
	DEB_MEMBER_FUNCT();
	size= PixelSize;
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getImageType(ImageType& type)
{
	DEB_MEMBER_FUNCT();
	try
	{
		PixelSizeEnums ps = Camera_->PixelSize.GetValue();
		switch( ps )
		{
			case PixelSize_Bpp8:
				type= Bpp8;
			break;
		
			case PixelSize_Bpp12:
				type= Bpp12;
			break;
		
			case PixelSize_Bpp16: //- this is in fact 12 bpp inside a 16bpp image
				type= Bpp16;
			break;
		
			default:
				type= Bpp10;
			break;
		}
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }		

}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getDetectorType(string& type)
{
	DEB_MEMBER_FUNCT();
	ostringstream os;
	try
	{
		os << devices_[0].GetVendorName();
		type= os.str();
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }		
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getDetectorModel(string& type)
{
	DEB_MEMBER_FUNCT();
	ostringstream os;
	try
	{
		os << devices_[0].GetModelName();
		type= os.str();
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }			
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::setMaxImageSizeCallbackActive(bool cb_active)
{  
	m_mis_cb_act = cb_active;
}

//-----------------------------------------------------
//
//-----------------------------------------------------
BufferCtrlMgr& Camera::getBufferMgr()
{
	return m_buffer_ctrl_mgr;
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::setTrigMode(TrigMode mode)
{
    DEB_MEMBER_FUNCT();
    DEB_PARAM() << DEB_VAR1(mode);
	
	try
	{
		if ( mode == IntTrig )
		{
		  //- INTERNAL
		  this->Camera_->TriggerMode.SetValue( TriggerMode_Off );
		  ////@@@@ this mode is disabled !! TO DO later
		  ////this->Camera_->AcquisitionFrameRateEnable.SetValue( true );
		  //////////////////////////////////////////////////////////////////
		}
		else if ( mode == ExtGate )
		{
		  //- EXTERNAL - TRIGGER WIDTH
		  this->Camera_->TriggerMode.SetValue( TriggerMode_On );
		  this->Camera_->AcquisitionFrameRateEnable.SetValue( false );
		  this->Camera_->ExposureMode.SetValue( ExposureMode_TriggerWidth );
		}		
		else //ExtTrigSingle
		{
		  //- EXTERNAL - TIMED
		  this->Camera_->TriggerMode.SetValue( TriggerMode_On );
		  this->Camera_->AcquisitionFrameRateEnable.SetValue( false );
		  this->Camera_->ExposureMode.SetValue( ExposureMode_Timed );
		}
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }		
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getTrigMode(TrigMode& mode)
{
    DEB_MEMBER_FUNCT();
	try
	{
		if (this->Camera_->TriggerMode.GetValue() == TriggerMode_Off)
			mode = IntTrig;
		else if (this->Camera_->ExposureMode.GetValue() == ExposureMode_TriggerWidth)
			mode = ExtGate;
		else //ExposureMode_Timed
			mode = ExtTrigSingle;
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }		
    DEB_RETURN() << DEB_VAR1(mode);
}


//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::setExpTime(double exp_time_ms)
{
	DEB_MEMBER_FUNCT();
	cout<<"Camera::setExpTime("<<exp_time_ms<<")"<<endl;
	DEB_PARAM() << DEB_VAR1(exp_time_ms);

	// from ImgGrabber !!
    // For Basler camera, there are 3 GenICam attributes that can be set
    // for exposure time setup :
    //    + ExposureTimeAbs      (float, microseconds)
    //    + ExposureTimeBaseAbs  (float, microsseconds)
    //    + ExposureTimeRaw      (integer, no unit)
    //
    // with : ExposureTimeAbs = ExposureTimeBaseAbs * ExposureTimeRaw
	try
	{
		Camera_->ExposureTimeBaseAbs.SetValue(80);//min allowed
		double raw = ::ceil( exp_time_ms / 50 );
		Camera_->ExposureTimeRaw.SetValue(static_cast<uint32_t>(raw));
		Camera_->ExposureTimeBaseAbs.SetValue(1E3 * exp_time_ms / Camera_->ExposureTimeRaw.GetValue());
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }		
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getExpTime(double& exp_time_ms)
{
	DEB_MEMBER_FUNCT();
	try
	{
		double value = 1.0E-3 * static_cast<double>(Camera_->ExposureTimeAbs.GetValue());	
		exp_time_ms = value;
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }			
	DEB_RETURN() << DEB_VAR1(exp_time_ms);
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::setLatTime(double lat_time)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(lat_time);
	/////@@@@ TODO if necessary
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getLatTime(double& lat_time)
{
	DEB_MEMBER_FUNCT();
	/////@@@@ TODO if necessary
	lat_time = 1;
	DEB_RETURN() << DEB_VAR1(lat_time);
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::setNbFrames(int nb_frames)
{
	DEB_MEMBER_FUNCT();
	DEB_PARAM() << DEB_VAR1(nb_frames);
	m_nb_frames = nb_frames;
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getNbFrames(int& nb_frames)
{
	DEB_MEMBER_FUNCT();
	nb_frames = m_nb_frames;
	DEB_RETURN() << DEB_VAR1(nb_frames);
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getStatus(Camera::Status& status)
{
	DEB_MEMBER_FUNCT();
	status = m_status;
	DEB_RETURN() << DEB_VAR1(DEB_HEX(status));
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::getFrameRate(double& frame_rate)
{
	DEB_MEMBER_FUNCT();
	try
	{
		frame_rate = static_cast<double>(Camera_->ResultingFrameRateAbs.GetValue());
	}
	catch (GenICam::GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred!" << endl << e.GetDescription() << endl;
        throw LIMA_HW_EXC(Error, e.GetDescription());
    }		
	DEB_RETURN() << DEB_VAR1(frame_rate);
}


//-----------------------------------------------------
//
//-----------------------------------------------------
void Camera::handle_message( yat::Message& msg )  throw( yat::Exception )
{
  try
  {
    switch ( msg.type() )
    {
      //-----------------------------------------------------	
      case yat::TASK_INIT:
      {
        std::cout <<"Camera::->TASK_INIT"<<std::endl;          
      }
      break;
      //-----------------------------------------------------    
      case yat::TASK_EXIT:
      {
        std::cout <<"Camera::->TASK_EXIT"<<std::endl;                
      }
      break;
      //-----------------------------------------------------    
      case yat::TASK_TIMEOUT:
      {
		std::cout <<"Camera::->TASK_TIMEOUT"<<std::endl;       
      }
      break;
      //-----------------------------------------------------    
      case DLL_START_MSG:	
      {
		std::cout <<"Camera::->DLL_START_MSG"<<std::endl;
		m_stop_already_done = false;
		this->post(new yat::Message(DLL_GET_IMAGE_MSG), kPOST_MSG_TMO);
      }
      break;
      //-----------------------------------------------------
      case DLL_GET_IMAGE_MSG:
      {
		std::cout <<"Camera::->DLL_GET_IMAGE_MSG"<<std::endl;
		GetImage();
      }
      break;	
      //-----------------------------------------------------
      case DLL_STOP_MSG:
      {
		std::cout <<"Camera::->DLL_STOP_MSG"<<std::endl;
		FreeImage();
      }
      break;
      //-----------------------------------------------------
    }
  }
  catch( yat::Exception& ex )
  {
      std::cout << "Error : " << ex.errors[0].desc;
    throw;
  }
}

//-----------------------------------------------------
// Constructor allocates the image buffer
//-----------------------------------------------------
CGrabBuffer::CGrabBuffer(size_t ImageSize)
{
    m_pBuffer = new uint8_t[ ImageSize ];
    if (NULL == m_pBuffer)
    {
        GenICam::GenericException e("Not enough memory to allocate image buffer", __FILE__, __LINE__);
        throw e;
    }
}

//-----------------------------------------------------
// Freeing the memory
//-----------------------------------------------------
CGrabBuffer::~CGrabBuffer()
{
    if (NULL != m_pBuffer)
        delete[] m_pBuffer;
}
//-----------------------------------------------------