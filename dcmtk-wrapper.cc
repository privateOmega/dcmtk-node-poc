#include <napi.h>
#include <dcmtk/config/osconfig.h> /* make sure OS specific configuration is included first */

#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmdata/cmdlnarg.h>
#include <dcmtk/ofstd/ofconapp.h>
#include <dcmtk/dcmdata/dcuid.h>     /* for dcmtk version name */
#include <dcmtk/dcmjpeg/djdecode.h>  /* for dcmjpeg decoders */
#include <dcmtk/dcmjpeg/djencode.h>  /* for dcmjpeg encoders */
#include <dcmtk/dcmjpeg/djrplol.h>   /* for DJ_RPLossless */
#include <dcmtk/dcmjpeg/djrploss.h>  /* for DJ_RPLossy */
#include <dcmtk/dcmjpeg/dipijpeg.h>  /* for dcmimage JPEG plugin */
#include <dcmtk/dcmimage/diregist.h> /* include to support color images */

#ifdef WITH_ZLIB
#include <zlib.h> /* for zlibVersion() */
#endif

static Napi::Value dcmcjpeg(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 2)
  {
    Napi::Error::New(env, "Wrong number of args").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString() || !info[1].IsString())
  {
    Napi::Error::New(env, "Wrong args").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string str1 = info[0].ToString().Utf8Value();
  std::string str2 = info[1].ToString().Utf8Value();
  const char *opt_ifname = str1.c_str();
  const char *opt_ofname = str2.c_str();

  E_FileReadMode opt_readMode = ERM_autoDetect;
  E_TransferSyntax opt_ixfer = EXS_Unknown;
  E_GrpLenEncoding opt_oglenc = EGL_recalcGL;
  E_EncodingType opt_oenctype = EET_ExplicitLength;
  E_PaddingEncoding opt_opadenc = EPD_noChange;
  OFCmdUnsignedInt opt_filepad = 0;
  OFCmdUnsignedInt opt_itempad = 0;
  OFBool opt_acceptWrongPaletteTags = OFFalse;
  OFBool opt_acrNemaCompatibility = OFFalse;

  // JPEG options
  E_TransferSyntax opt_oxfer = EXS_JPEGProcess14SV1;
  OFCmdUnsignedInt opt_selection_value = 6;
  OFCmdUnsignedInt opt_point_transform = 0;
  OFCmdUnsignedInt opt_quality = 90;
  OFBool opt_huffmanOptimize = OFTrue;
  OFCmdUnsignedInt opt_smoothing = 0;
  int opt_compressedBits = 0; // 0=auto, 8/12/16=force
  E_CompressionColorSpaceConversion opt_compCSconversion = ECC_lossyYCbCr;
  E_DecompressionColorSpaceConversion opt_decompCSconversion = EDC_photometricInterpretation;
  OFBool opt_predictor6WorkaroundEnable = OFFalse;
  OFBool opt_cornellWorkaroundEnable = OFFalse;
  OFBool opt_forceSingleFragmentPerFrame = OFFalse;
  E_SubSampling opt_sampleFactors = ESS_422;
  OFBool opt_useYBR422 = OFTrue;
  OFCmdUnsignedInt opt_fragmentSize = 0; // 0=unlimited
  OFBool opt_createOffsetTable = OFTrue;
  int opt_windowType = 0; /* default: no windowing; 1=Wi, 2=Wl, 3=Wm, 4=Wh, 5=Ww, 6=Wn, 7=Wr */
  OFCmdUnsignedInt opt_windowParameter = 0;
  OFCmdFloat opt_windowCenter = 0.0, opt_windowWidth = 0.0;
  E_UIDCreation opt_uidcreation = EUC_default;
  OFBool opt_secondarycapture = OFFalse;
  OFCmdUnsignedInt opt_roiLeft = 0, opt_roiTop = 0, opt_roiWidth = 0, opt_roiHeight = 0;
  OFBool opt_usePixelValues = OFTrue;
  OFBool opt_useModalityRescale = OFFalse;
  OFBool opt_trueLossless = OFTrue;
  OFBool opt_lossless = OFTrue;
  OFBool lossless = OFTrue; /* see opt_oxfer */

  // register global decompression codecs
  DJDecoderRegistration::registerCodecs(
      opt_decompCSconversion,
      opt_uidcreation,
      EPC_default,
      opt_predictor6WorkaroundEnable,
      opt_cornellWorkaroundEnable,
      opt_forceSingleFragmentPerFrame);

  // register global compression codecs
  DJEncoderRegistration::registerCodecs(
      opt_compCSconversion,
      opt_uidcreation,
      opt_huffmanOptimize,
      OFstatic_cast(int, opt_smoothing),
      opt_compressedBits,
      OFstatic_cast(Uint32, opt_fragmentSize),
      opt_createOffsetTable,
      opt_sampleFactors,
      opt_useYBR422,
      opt_secondarycapture,
      OFstatic_cast(Uint32, opt_windowType),
      OFstatic_cast(Uint32, opt_windowParameter),
      opt_windowCenter,
      opt_windowWidth,
      OFstatic_cast(Uint32, opt_roiLeft),
      OFstatic_cast(Uint32, opt_roiTop),
      OFstatic_cast(Uint32, opt_roiWidth),
      OFstatic_cast(Uint32, opt_roiHeight),
      opt_usePixelValues,
      opt_useModalityRescale,
      opt_acceptWrongPaletteTags,
      opt_acrNemaCompatibility,
      opt_trueLossless);

  /* make sure data dictionary is loaded */
  if (!dcmDataDict.isDictionaryLoaded())
  {
    COUT << "no data dictionary loaded, check environment variable: "
         << DCM_DICT_ENVIRONMENT_VARIABLE << std::endl;
  }

  // open inputfile
  if ((opt_ifname == NULL) || (strlen(opt_ifname) == 0))
  {
    Napi::Error::New(env, "invalid filename: <empty string>").ThrowAsJavaScriptException();
    return env.Null();
  }

  COUT << "reading input file " << opt_ifname << std::endl;

  DcmFileFormat fileformat;
  OFCondition error = fileformat.loadFile(opt_ifname, opt_ixfer, EGL_noChange, DCM_MaxReadLength, opt_readMode);
  if (error.bad())
  {
    COUT << error.text() << ": reading file: " << opt_ifname << std::endl;
    Napi::Error::New(env, "read failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  DcmDataset *dataset = fileformat.getDataset();

  DcmXfer original_xfer(dataset->getOriginalXfer());
  if (original_xfer.isEncapsulated())
  {
    COUT << "DICOM file is already compressed, converting to uncompressed transfer syntax first" << std::endl;
    if (EC_Normal != dataset->chooseRepresentation(EXS_LittleEndianExplicit, NULL))
    {
      COUT << "no conversion from compressed original to uncompressed transfer syntax possible!" << std::endl;
      Napi::Error::New(env, "no conversion from compressed original to uncompressed transfer syntax possible!").ThrowAsJavaScriptException();
      return env.Null();
    }
  }

  OFString sopClass;
  if (fileformat.getMetaInfo()->findAndGetOFString(DCM_MediaStorageSOPClassUID, sopClass).good())
  {
    /* check for DICOMDIR files */
    if (sopClass == UID_MediaStorageDirectoryStorage)
    {
      COUT << "DICOMDIR files (Media Storage Directory Storage SOP Class) cannot be compressed!" << std::endl;
      Napi::Error::New(env, "its a dicomdir").ThrowAsJavaScriptException();
      return env.Null();
    }
  }

  COUT << "Convert DICOM file to compressed transfer syntax" << std::endl;

  DcmXfer opt_oxferSyn(opt_oxfer);

  // create representation parameters for lossy and lossless
  DJ_RPLossless rp_lossless(OFstatic_cast(int, opt_selection_value), OFstatic_cast(int, opt_point_transform));
  DJ_RPLossy rp_lossy(OFstatic_cast(int, opt_quality));

  const DcmRepresentationParameter *rp = &rp_lossy;
  if (lossless)
    rp = &rp_lossless;

  if (dataset->chooseRepresentation(opt_oxfer, rp).good() && dataset->canWriteXfer(opt_oxfer))
  {
    COUT << "Output transfer syntax " << opt_oxferSyn.getXferName() << " can be written" << std::endl;
  }
  else
  {
    COUT << "no conversion to transfer syntax " << opt_oxferSyn.getXferName() << " possible!" << std::endl;
    Napi::Error::New(env, "conversion not possible").ThrowAsJavaScriptException();
    return env.Null();
  }

  COUT << "creating output file " << opt_ofname << std::endl;

  fileformat.loadAllDataIntoMemory();
  error = fileformat.saveFile(opt_ofname, opt_oxfer, opt_oenctype, opt_oglenc,
                              opt_opadenc, OFstatic_cast(Uint32, opt_filepad), OFstatic_cast(Uint32, opt_itempad), EWM_updateMeta);

  if (error.bad())
  {
    COUT << error.text() << ": writing file: " << opt_ofname << std::endl;
    Napi::Error::New(env, "conversion failed").ThrowAsJavaScriptException();
    return env.Null();
  }

  COUT << "conversion successful" << std::endl;

  // deregister global codecs
  DJDecoderRegistration::cleanup();
  DJEncoderRegistration::cleanup();

  return Napi::String::New(env, "successfully converted");
}

static Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  exports.Set(Napi::String::New(env, "dcmcjpeg"), Napi::Function::New(env, dcmcjpeg));

  return exports;
}

NODE_API_MODULE(dcmtkWrapper, Init)
