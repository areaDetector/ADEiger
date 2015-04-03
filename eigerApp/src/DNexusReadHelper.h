/*! 
 *  \brief     C++ Helper functions to interface HDF5 c-library calls
 *  \details   This class implements some of the functionality needed to read
 *             hdf5 files with a NeXus header written by Dectris EIGER detectors.
 *             Remarks:
 *             1.) These functions illustrate only how to use the HDF5 library in
 *                 order to read the files written by Dectris EIGER detectors 
 *             1.) Be aware that these functions are not (completely) RAII,
     *             so e.g. not every HDF5 group that is opened is also safely closed
 *                 when exiting the scope on every possible way!
 *             2.) No exceptions are thrown by this class. Instead, each 
 *                 function returns a bool, which is true on success.
 *             3.) It is explicitely stated which of the H5*open function is used,
 *                 instead of using the macros.
 *
 *  \author    Michael Rissi
 *  \author    Contact: support@dectris.com
 *  \version   0.1
 *  \date      21.11.2012
 *  \copyright See General Terms and Conditions (GTC) on http://www.dectris.com
 * 
 */


#ifndef DNEXUSREADHELPER_H
#define DNEXUSREADHELPER_H

#include <algorithm>
#include <hdf5.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <cstdlib>
#include <typeinfo>



namespace DNexusReadHelper
{
  template<class T>
    hid_t determineHDF5Datatype()
    {
      /// determines the hdf5 datatype from the template parameter <T> ///
      hid_t h5Datatype;
      if (typeid (T) == typeid (int))
	h5Datatype = H5T_NATIVE_INT;
      else if (typeid (T) == typeid (short))
	h5Datatype = H5T_NATIVE_SHORT;
      else if (typeid (T) == typeid (long))
	h5Datatype = H5T_NATIVE_LONG;
      else if (typeid (T) == typeid (long long))
	h5Datatype = H5T_NATIVE_LLONG;
      else if (typeid (T) == typeid (unsigned int))
	h5Datatype = H5T_NATIVE_UINT;
      else if (typeid (T) == typeid (unsigned short))
	h5Datatype = H5T_NATIVE_USHORT;
      else if (typeid (T) == typeid (unsigned long))
	h5Datatype = H5T_NATIVE_ULONG;
      else if (typeid (T) == typeid (unsigned long long))
	h5Datatype = H5T_NATIVE_ULLONG;
      else if (typeid (T) == typeid (float))
	h5Datatype = H5T_NATIVE_FLOAT;
      else if (typeid (T) == typeid (double))
	h5Datatype = H5T_NATIVE_DOUBLE;
      //for bools //
      else if (typeid (T) == typeid (unsigned char))
	h5Datatype = H5T_NATIVE_UCHAR;
      else
	{
	  std::cout << "not a implemented hdf5 datatype" << std::endl;
	  return -1;
	}
      return h5Datatype;
    }
  
  template<class T>
    bool ReadDatasetItem(hid_t groupID, const std::string name,  std::vector<T> *vec, std::vector<hsize_t> *dim, std::string *units)
    {
      /// reads a dataset into the std::vector<> vec, including its dimensions dim, and if available its units ///

      hid_t dataid = H5Dopen2(groupID,name.c_str(), H5P_DEFAULT);
      if(dataid<0)
	{
	  std::cout<<"cannot open dataset"<<std::endl;
	  
	  return false;      
	}
      
      
      hid_t dataspace = H5Dget_space(dataid);
      if(dataspace<0)
	{
	  std::cout<<"cannot get dataspace"<<std::endl;
	  return false;
	}
      int rank    =  H5Sget_simple_extent_ndims(dataspace);
      if(rank<0)
	{
	  std::cout<<"cannot get rank"<<std::endl;
	  return false;
	}
      dim->resize(rank);
      herr_t err = H5Sget_simple_extent_dims(dataspace, dim->data(), NULL);
      if(err<0)
	{
	  std::cout<<"cannot get dimensions"<<std::endl;
	  return false;
	}
      
      size_t totSize = 1;   /// total size (in units of T)
      for(uint i=0; i<dim->size();++i) // not allowed to use C++11
	totSize *= dim->at(i);
      
      /// the derived datatype from <T> ///
      hid_t h5DataType = determineHDF5Datatype<T>();
      
      /// the saved datatype in the hdf5 file //
      hid_t  h5FileDataType= H5Dget_type(dataid);
      
      htri_t h5equal = H5Tequal(h5DataType, h5FileDataType);
      
      if(h5equal != true || h5equal < 0)  /// true is != 0, false is 0. <0 means an error occured in function call. 
	{
	  std::cout<<"wrong datatype of <T>"<<std::endl;
	  return false;
	}
      
      
      if(h5DataType<0)
	return false;
      
            
      vec->resize(totSize); 
      err = H5Dread(dataid, h5DataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, vec->data());
      if(err<0)
	{
	  std::cout<<"cannot read dataset"<<std::endl;
	  return false;
	}
      
      /// read the units attribute ///
      /// check first if units attribute exists ///
      htri_t unitsExists = H5Aexists_by_name(dataid, ".", "unit", H5P_DEFAULT);
      if(unitsExists < 0) /// something went wrong
	{
	  std::cout<<"cannot read units attribute"<<std::endl;
	  return false;
	}

      if(unitsExists>0) /// HDF5 true
	{
	  hid_t attr_id = H5Aopen_by_name(dataid, ".", "unit", H5P_DEFAULT, H5P_DEFAULT);

	  
	  if(attr_id<0)
	    {
	      std::cout<<"cannot open attribute units"<<std::endl;
	      return false;
	}
      
	  
	  hid_t attr_type = H5Aget_type (attr_id);
	  hsize_t numchars = H5Tget_size(attr_type);
      
	  
	  char cunits[numchars];
	  herr_t status = H5Aread (attr_id, attr_type, cunits);
	  units->clear();
	  units->insert(0, cunits, numchars);
	  H5Aclose(attr_id);
	}
      else
      {
    	  std::cout<<"DEBUG DIEGO - units Exists = 0"<<std::endl;
    	  std::cout<<"DEBUG DIEGO - No field unit for "<<name<<std::endl;
    	 *units = "";
      }
      H5Dclose(dataid);
      
      return true;
      
      
    } 
  
  
  bool ReadDatasetItem(hid_t groupID, const std::string name,  std::vector<std::string> *vec, std::vector<hsize_t> *dim, std::string *units)
  {

    /// specialisation for std::string datasets ///

    hid_t dataid = H5Dopen2(groupID,name.c_str(), H5P_DEFAULT);
    if(dataid<0)
      {
	std::cout<<"cannot open dataset"<<std::endl;
	
	return false;      
      }
    
    
    hid_t dataspace = H5Dget_space(dataid);
    if(dataspace<0)
      {
	std::cout<<"cannot get dataspace"<<std::endl;
	return false;
      }
    int rank    =  H5Sget_simple_extent_ndims(dataspace);
    if(rank<0)
      {
	std::cout<<"cannot get rank"<<std::endl;
	return false;
      }
    
    dim->resize(rank);
    herr_t err = H5Sget_simple_extent_dims(dataspace, dim->data(), NULL);
    if(err<0)
      {
	std::cout<<"cannot get dimensions"<<std::endl;
	return false;
      }
    
    size_t totSize = 1;   /// total size (in units of T)
    for(uint i=0; i<dim->size();++i) 
      totSize *= dim->at(i);
    
    
    hid_t h5DataType = H5Dget_type(dataid);
    hsize_t numchars = H5Tget_size(h5DataType);
    
    char cstr[numchars*totSize]; // ugh
    
    err = H5Dread(dataid, h5DataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, cstr);
    if(err<0)
      {
	std::cout<<"cannot read dataset"<<std::endl;
	return false;
      }
    for(int i=0; i<totSize; ++i)
      {
	char *buf = &cstr[i*numchars];
	std::string str(buf, std::find(buf, buf+numchars, '\0'));
	vec->push_back(str);
      }
    
    return true;
    
  }
  
  template<class T> ///T: uint16_t (Eiger), uint32_t (Pilatus), float
    bool readOneImage(hid_t fid, std::vector<T> *img,  std::vector<hsize_t> *dim)
    {

	  hid_t gid_entry = H5Gopen2(fid, "/entry", H5P_DEFAULT);
      if(gid_entry<0)
      {
    	  std::cout<<"cannot open entry group"<<std::endl;
    	  return false;
      }

      hid_t dataid = H5Dopen2(gid_entry, "data", H5P_DEFAULT);
      if(dataid<0)
      {
    	  H5Gclose(gid_entry);
    	  std::cout<<"cannot open data set 'data'"<<std::endl;
    	  return false;
      }

       size_t totSize = 1;   /// total size (in units of T)
      for(uint i=0; i<dim->size();++i)
      {
    	  totSize *= dim->at(i);

      }

      /// the derived datatype from <T> ///
      hid_t h5DataType = determineHDF5Datatype<T>();

      /// the saved datatype in the hdf5 file //
      hid_t  h5FileDataType= H5Dget_type(dataid);

      htri_t h5equal = H5Tequal(h5DataType, h5FileDataType);

      if(h5equal != true || h5equal < 0)  /// true is != 0, false is 0. <0 means an error occured in function call.
      {
    	  std::cout<<"wrong datatype of <T>"<<std::endl;
    	  return false;
      }

      hid_t dataspace = H5Dget_space(dataid);
      if(dataspace<0)
      {
    	  std::cout<<"cannot get dataspace"<<std::endl;
    	  return false;
      }
      int rank    =  H5Sget_simple_extent_ndims(dataspace);
      if(rank<0)
      {
    	  std::cout<<"cannot get rank"<<std::endl;
    	  return false;
      }

      if(rank != 3) /// images dataset must be 3D!
      {
    	  std::cout<<"image dataset not 3D"<<std::endl;
    	  return false;
      }

      hsize_t dims[3];
      hsize_t maxdims[3];
      H5Sget_simple_extent_dims(dataspace, dims, maxdims);

      dim->at(0) = dims[1]; // dims[0] is z index
      dim->at(1) = dims[2];

      img->resize(dims[1]*dims[2]); /// image is 1D in memory, dimensions are saved in std::array<> dim

      /// create hyperslab ////
      hsize_t count[3];
      count[0] = 1; /// hyperslab is one image
      count[1]  = dims[1];
      count[2]  = dims[2];

      hid_t memspace_id = H5Screate_simple(3, count, NULL);

      /*
      /// need image_nr_low again ///
      hid_t attr_id = H5Aopen_by_name(gid_entry,  "data" , "image_nr_low", H5P_DEFAULT, H5P_DEFAULT);
      if(attr_id<0)
	  {
	    H5Gclose(gid_entry);
	    return  false;
	  }


      hid_t attr_type = H5Aget_type (attr_id);
      int image_nr_low;
      herr_t status = H5Aread (attr_id, attr_type, &image_nr_low);
      if(status<0)
	  {
	    std::cout<<"cannot read image_nr_low attribute"<<std::endl;
	    H5Aclose(attr_id);
	    H5Gclose(gid_entry);
	    return false;
	  }
      H5Aclose(attr_id);
	*/
	/// compute the offset of image number:
     hsize_t offset[3];
    /*
	offset[0] = imageNr - image_nr_low;
	if(offset[0] <0 )
	 {
	    std::cout<<"image in wrong dataset. LUT correct?"<<std::endl;
	    return false;
	  }
	*/
    offset[0] = 0;
	offset[1] = 0;
	offset[2] = 0;

	/// select the hyperslab ///
	herr_t errstatus = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);
	if(errstatus<0)
	  {
	    std::cout<<"cannot select hyperslab"<<std::endl;
	    return false;
	  }
	/// and finally read the image ///
	herr_t status = H5Dread(dataid, h5FileDataType, memspace_id, dataspace , H5P_DEFAULT, img->data());
	if(status<0)
	  {
	    std::cout<<"cannot read data"<<std::endl;
	    return false;
	  }

	H5Sclose( memspace_id);

	H5Dclose(dataid);
	H5Gclose(gid_entry);
	return true;

    }
  
  
  bool CreateLUT(hid_t fid, std::map<size_t, std::string> *lut, uint32_t imgNumStart)
  {
    /// The LUT maps the number of an image to the link to the dataset, where the corresponding image can be found ///
    

    //int linkCnt = 0;
    while(true)
      {
	char dataName[1024];
	sprintf(dataName, "data_%06d", imgNumStart++);
		
	hid_t gid_entry = H5Gopen2(fid, "/entry", H5P_DEFAULT);
	if(gid_entry<0)
	  {
	    std::cout<<"cannot open entry group"<<std::endl;
	    //H5Gclose(gid_entry);
	    return false;
	  }
	
	
	
	/// check if link exists ///
	htri_t linkExists = H5Lexists(gid_entry, dataName, H5P_DEFAULT);
	if(linkExists < 0)
	  {
	    std::cout<<"problem with H5Lexists"<<std::endl;
	    H5Gclose(gid_entry);
	    return false;

	  }
	if(linkExists == false) /// link does not exist
	  {
		std::cout<<dataName<<" link does not exist!"<<std::endl;
	    break;
	  }
	else
		std::cout<<dataName<<" link exist!"<<std::endl;
	
	/// read the image_nr_low and image_nur_high attributes ///
	/// switch off annoying hdf5 warnings:
	hid_t error_stack = H5E_DEFAULT;
	/* Save old error handler */
	void *old_client_data;
	H5E_auto2_t old_func;
	H5Eget_auto2(error_stack, &old_func, &old_client_data);
	hid_t err= H5Eset_auto2(error_stack, NULL, NULL);

	htri_t attrExists = H5Aexists_by_name(gid_entry,  dataName , "image_nr_low", H5P_DEFAULT);

	/// switch hdf5 warnings on again
	err =  H5Eset_auto2(error_stack, old_func, old_client_data);
	if(attrExists<0 || attrExists == false)
	  {


	    return true; /// this is ok, as the file where the link points to does not exist. This happens when the user enters too many
	    /// nimages and stops the data taking before all images where taken.
	  }



	hid_t attr_id = H5Aopen_by_name(gid_entry,  dataName , "image_nr_low", H5P_DEFAULT, H5P_DEFAULT);
	
	if(attr_id<0)
	  {

	    H5Gclose(gid_entry);
	    std::cout<<"cannot open attribute: image_nr_low"<<std::endl;
	    return false;
	  }
	
	hid_t attr_type = H5Aget_type (attr_id);
	int image_nr_low;
	herr_t status = H5Aread (attr_id, attr_type, &image_nr_low);
	if(status<0)
	  {
	    std::cout<<"cannot read image_nr_low attribute"<<std::endl;
	    H5Aclose(attr_id);
	    H5Gclose(gid_entry);
	    return false;
	  }
	H5Aclose(attr_id);
	attr_id = H5Aopen_by_name(gid_entry, dataName , "image_nr_high", H5P_DEFAULT, H5P_DEFAULT);
	if(attr_id<0)
	  {
	    std::cout<<"cannot read image_nr_high attribute"<<std::endl;
	    H5Aclose(attr_id);
	    H5Gclose(gid_entry);
	    return false;
	  }
	
	attr_type = H5Aget_type (attr_id);
	int image_nr_high;
	status = H5Aread (attr_id, attr_type, &image_nr_high);
	if(status<0)
	  {
	    H5Aclose(attr_id);
	    H5Gclose(gid_entry);
	    std::cout<<"cannot read image_nr_high attribute"<<std::endl;
	    return false;
	  }
	
	/// and create the map ///
	for(int nimg=image_nr_low; nimg<=image_nr_high; nimg++)
	  {
		std::cout<<"dataName = "<<std::string(dataName)<<std::endl;
	    (*lut)[nimg] = std::string(dataName);
	  }
	
	H5Aclose(attr_id);
	H5Gclose(gid_entry);

      }
    return true;
  }
  
  template<class T> ///T: uint16_t (Eiger), uint32_t (Pilatus), float
    bool ReadImage(size_t imageNr, std::map<size_t, std::string> lut, hid_t fid, std::vector<T> *img,  std::vector<hsize_t> *dim)
    {

      /// reads an image and its dimensions, using the above created LUT.

      std::string linkName = lut[imageNr];
      if(linkName=="")
	{
	  std::cout<<"image does not exist"<<std::endl;
	  return false;
	}
     
      
      hid_t gid_entry = H5Gopen2(fid, "/entry", H5P_DEFAULT);
      if(gid_entry<0)
	{
	  std::cout<<"cannot open entry group"<<std::endl;
	  return false;
	}
      
      
      hid_t dataid = H5Dopen2(gid_entry, linkName.c_str(), H5P_DEFAULT);
      if(dataid<0)
	{
	  H5Gclose(gid_entry);
	  std::cout<<"cannot open data set "<<linkName<<std::endl;
	  return false;
	}

       size_t totSize = 1;   /// total size (in units of T)
      for(uint i=0; i<dim->size();++i) 
	{
	  totSize *= dim->at(i);
	  
	}
      
      /// the derived datatype from <T> ///
      hid_t h5DataType = determineHDF5Datatype<T>();
      
      /// the saved datatype in the hdf5 file //
      hid_t  h5FileDataType= H5Dget_type(dataid);
      
      htri_t h5equal = H5Tequal(h5DataType, h5FileDataType);
      
      if(h5equal != true || h5equal < 0)  /// true is != 0, false is 0. <0 means an error occured in function call. 
	{
	  std::cout<<"wrong datatype of <T>"<<std::endl;
	  return false;
	}
         
      hid_t dataspace = H5Dget_space(dataid);
      if(dataspace<0)
	{
	  std::cout<<"cannot get dataspace"<<std::endl;
	  return false;
	}
      int rank    =  H5Sget_simple_extent_ndims(dataspace);
      if(rank<0)
	{
	  std::cout<<"cannot get rank"<<std::endl;
	  return false;
	}

      if(rank != 3) /// images dataset must be 3D!
	{
	  std::cout<<"image dataset not 3D"<<std::endl;
	  return false;
	}

      hsize_t dims[3];
      hsize_t maxdims[3];
      H5Sget_simple_extent_dims(dataspace, dims, maxdims);
      
      dim->at(0) = dims[1]; // dims[0] is z index
      dim->at(1) = dims[2];
      
      img->resize(dims[1]*dims[2]); /// image is 1D in memory, dimensions are saved in std::array<> dim

      /// create hyperslab ////
      hsize_t count[3];
      count[0] = 1; /// hyperslab is one image
      count[1]  = dims[1];
      count[2]  = dims[2];

      hid_t memspace_id = H5Screate_simple(3, count, NULL);

      /// need image_nr_low again ///
      hid_t attr_id = H5Aopen_by_name(gid_entry,  linkName.c_str() , "image_nr_low", H5P_DEFAULT, H5P_DEFAULT);
	if(attr_id<0)
	  {
	    H5Gclose(gid_entry);
	    return  false;
	  }
	
	hid_t attr_type = H5Aget_type (attr_id);
	int image_nr_low;
	herr_t status = H5Aread (attr_id, attr_type, &image_nr_low);
	if(status<0)
	  {
	    std::cout<<"cannot read image_nr_low attribute"<<std::endl;
	    H5Aclose(attr_id);
	    H5Gclose(gid_entry);
	    return false;
	  }
	H5Aclose(attr_id);

	/// compute the offset of image number: 
	hsize_t offset[3];
	offset[0] = imageNr - image_nr_low;
	if(offset[0] <0 )
	  {
	    std::cout<<"image in wrong dataset. LUT correct?"<<std::endl;
	    return false;
	  }
	offset[1] = 0;
	offset[2] = 0;

	/// select the hyperslab ///
	herr_t errstatus = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);
	if(errstatus<0)
	  {
	    std::cout<<"cannot select hyperslab"<<std::endl;
	    return false;
	  }
	/// and finally read the image ///
	status = H5Dread(dataid, h5FileDataType, memspace_id, dataspace , H5P_DEFAULT, img->data());
	if(status<0)
	  {
	    std::cout<<"cannot read data"<<std::endl;
	    return false;
	  }

	H5Sclose( memspace_id);

	H5Dclose(dataid);
	H5Gclose(gid_entry);
	return true;

    }
  
template<class T>
  inline T getPixelValue(size_t pix_x, size_t pix_y, const std::vector<T> &img,  const std::vector<hsize_t> &dim)
  {
    if(pix_x >= dim[0] || pix_y >= dim[1])
      {
	std::cout<<"invalid pixel"<<std::endl;
	return -1;
      }
    
    size_t addr = pix_y*dim[0] + pix_x;
    return img[addr];

  }
 template<class T>
 bool DetermineProteinStructure(const std::vector<T> &img,  std::vector<hsize_t> &dim)
   {
     /// Now that is the easy part. ///
     /// just kidding!              ///
     return true;
   }
  
} /// namespace 
#endif
