#include "descriptors.h"

using namespace std;
USING_PART_OF_NAMESPACE_EIGEN


/****************************************************************************
*************  Generally Useful Functions
*****************************************************************************/


//! This is an example of how to set up the descriptors.  Using this one is ill-advised as it may change without notice.
vector<ImageDescriptor*> setupImageDescriptorsExample() {
  vector<ImageDescriptor*> d;

  SuperpixelColorHistogram* sch1 = new SuperpixelColorHistogram(20, 0.5, 50, string("hue"));
  SuperpixelColorHistogram* sch2 = new SuperpixelColorHistogram(5, 0.5, 50, string("hue"), NULL, sch1);
  SuperpixelColorHistogram* sch3 = new SuperpixelColorHistogram(5, 1, 50, string("hue"), NULL, sch1);
  SuperpixelColorHistogram* sch4 = new SuperpixelColorHistogram(5, .25, 50, string("hue"), NULL, sch1);

  d.push_back(sch1);
  d.push_back(sch2);
  d.push_back(sch3);
  d.push_back(sch4);

  d.push_back(new IntensityPatch(50, 1, true));
  d.push_back(new PatchStatistic(string("variance"), (Patch*)d.back()));
  d.push_back(new IntensityPatch(40, .5, true));
  d.push_back(new IntensityPatch(20, 1, true));
  d.push_back(new IntensityPatch(80, .25, true));
  d.push_back(new IntensityPatch(120, 1.0/6.0, true));
  return d;
}


void whiten(MatrixXf* m) {
  float var=0.0;
  float mean = m->sum() / m->rows();

  for(int i=0; i<m->rows(); i++) {
    (*m)(i,0) = (*m)(i,0) - mean;
    var += pow((*m)(i,0), 2);
  }
  var /= m->rows();
  if(abs(m->sum() / m->rows()) > 1e-2) {
    cout << "mean: " << m->sum() / m->rows() << endl;
    cout << m->transpose() << endl;
    assert(abs(m->sum() / m->rows()) <= 1e-3);
  }

  if(var != 0.0) { 
    *m = *m / sqrt(var);
    
    // -- Check.
//     var = 0;
//     for(int i=0; i<m->rows(); i++) {
//       (*m)(i,0) = (*m)(i,0);
//       var += pow((*m)(i,0), 2);
//     }
//     var /= m->rows();
//     if(abs(var-1) >= 1e-3) {
//       cout << "var " << var << endl;
//       assert(abs(var-1) < 1e-3);
//     }
  }

  assert(!isnan((*m)(0,0)));
}



void inPaintNN(IplImage* img) {
  // -- Remove -1 (boundary) entries by nearest neighbor.
  bool all_assigned = false;
  while(!all_assigned) {
    all_assigned = true;
    for(int r=0; r<img->height; r++) {
      long* ptr = (long*)(img->imageData + r * img->widthStep);
      for(int c=0; c<img->width; c++) {
	if(*ptr >= 1) {
	  ptr++;
	  continue;
	}	

	if(c<img->width-1 && ptr[1] > 0) {
	  *ptr = ptr[1];
	  ptr++; 
	  continue;
	}
	if(c>0 && ptr[-1] > 0) {
	  *ptr = ptr[-1];
	  ptr++; 
	  continue;
	}
	if(r>0) {
	  long* ptr2 = (long*)(img->imageData + (r-1) * img->widthStep);
	  if(ptr2[c] > 0) {
	    *ptr = ptr2[c];
	    ptr++; 
	    continue;
	  }
	}
	if(r<img->height-1) {
	  long* ptr2 = (long*)(img->imageData + (r+1) * img->widthStep);
	  if(ptr2[c] > 0) {
	    *ptr = ptr2[c];
	    ptr++; 
	    continue;
	  }
	}
	all_assigned = false;
	ptr++;
      }
    }
  }
}


/****************************************************************************
*************  ImageDescriptor
****************************************************************************/

void ImageDescriptor::setImage(IplImage* img) {
  img_ = img;
  clearImageCache();
}

void ImageDescriptor::setPoint(int row, int col) {
  row_ = row;
  col_ = col;
  clearPointCache();
}

void ImageDescriptor::commonDebug() {
  IplImage* display = cvCloneImage(img_);
  cvResetImageROI(display);
  CvFont* numbFont = new CvFont();
  cvInitFont( numbFont, CV_FONT_VECTOR0, 1.0f, 1.0f, 0, 1);
  cvPutText(display, "+", cvPoint(col_,row_), numbFont, cvScalar(0,0,255));
  cvNamedWindow("Input Image");
  cvShowImage("Input Image", display);
  cvWaitKey(0);
  cvReleaseImage(&display);
}

bool ImageDescriptor::compute(MatrixXf** result, bool debug) {
  return false;
}

void ImageDescriptor::display(const MatrixXf& result) {
  cout << "Displaying..." << endl;
}


/****************************************************************************
*************  ImageDescriptor::Patch
****************************************************************************/


Patch::Patch(int raw_size, float scale) 
  : raw_size_(raw_size), scale_(scale), final_patch_(NULL), scaled_patch_(NULL)
{
  //Common patch constructor computation.
  cout << "Doing common constructor computation." << endl;
  size_ = (int) ((float)raw_size * scale);
  if(size_%2==0)
    size_ -= 1;

  
  char buf[100];
  sprintf(buf, "Patch_sz%d_scale%g", raw_size_, scale_);
  name_.assign(buf);
}

bool Patch::preCompute(bool debug) {
  if(debug)
    cout << "Computing " << name_ << endl;
  
  // -- final_patch_ should have been deallocated on clearPointCache(), which must be called by the user.
  if(final_patch_ != NULL || scaled_patch_ != NULL)
    cout << "WARNING: final_patch_  or scaled_patch_ has not been deallocated.  Have you called clearPointCache()?" << endl;

  // -- Catch edge cases.
  int half = ceil((float)raw_size_ / 2.0);
  if(row_-half < 0 || row_+half >= img_->height || col_-half < 0 || col_+half >= img_->width) {
    //cout << "Out of bounds; size: " << raw_size_ << ", row: " << row_ << ", col_: " << col_ << endl;
    return false;
  }
  
  // -- Get the scaled patch.
  //cout << "Setting roi to " << row_-half << " " << col_-half << " " << raw_size_ << endl;
  cvSetImageROI(img_, cvRect(col_-half, row_-half, raw_size_, raw_size_));
  scaled_patch_ = cvCreateImage(cvSize(size_, size_), img_->depth, img_->nChannels);
  cvResize(img_, scaled_patch_,CV_INTER_AREA);
  
  return true;
}
     
void Patch::clearPointCache() {
  cvReleaseImage(&scaled_patch_);
  cvReleaseImage(&final_patch_);
  final_patch_ = NULL;
  scaled_patch_ = NULL;
}

//void Patch::clearImageCache() {}

/****************************************************************************
*************  ImageDescriptor::Patch::IntensityPatch
****************************************************************************/

IntensityPatch::IntensityPatch(int raw_size, float scale, bool whiten)
  : Patch(raw_size, scale), whiten_(whiten)
{
  char buf[100];
  sprintf(buf, "_whiten%d_Intensity", whiten_);
  name_.append(buf);
  cout << "Creating " << name_ << endl;

  result_size_ = size_ * size_;
}

bool IntensityPatch::compute(MatrixXf** result, bool debug) {
  //Do common patch processing.  
  if(!preCompute(debug))
    return false;


  final_patch_ = cvCreateImage(cvSize(size_, size_), IPL_DEPTH_8U, 1);
  cvCvtColor(scaled_patch_, final_patch_, CV_BGR2GRAY);
  MatrixXf* res = new MatrixXf(result_size_,1);

  // -- Convert ipl to Newmat.
  int idx=0;
  for(int r=0; r<final_patch_->height; r++) {
    uchar* ptr = (uchar*)(final_patch_->imageData + r * final_patch_->widthStep);
    for(int c=0; c<final_patch_->width; c++) {
      (*res)(idx, 0) = *ptr;
      ptr++;
      idx++;
    }
  }

  // -- Set mean to 0, variance to 1 if appropriate and desired.
  if(whiten_)
    whiten(res);
  
  *result = res;

  // -- Display for debugging.
  if(debug) {
    cout << name_ << " dump: ";
    cout << (**result) << endl;
      
    IplImage* final_patch_rescaled = cvCreateImage(cvSize(500,500), IPL_DEPTH_8U, 1);
    cvResize(final_patch_, final_patch_rescaled, CV_INTER_NN);
    cvNamedWindow(name_.c_str());
    cvShowImage(name_.c_str(), final_patch_rescaled);
    commonDebug();

    cvReleaseImage(&final_patch_rescaled);
  }
   
  // -- Clean up.
  cvResetImageROI(img_);
  return true;
}

     
/****************************************************************************
*************  ImageDescriptor::PatchStatistic
****************************************************************************/

PatchStatistic::PatchStatistic(string type, Patch* patch) :
  type_(type), patch_(patch)
{
  char buf[200];
  sprintf(buf, "%s_PatchStatistic_%s", patch_->name_.c_str(), type_.c_str());
  name_.assign(buf);
  cout << "Creating " << name_ << endl;

  if(type_.compare("variance") == 0) {  
    result_size_ = 1;
  }
}

bool PatchStatistic::compute(MatrixXf** result, bool debug) {

  if(patch_ == NULL) {
    cout << "patch_ was null" << endl;
    return false; 
  }
  if(patch_->final_patch_ == NULL) {
    cout << "final_patch_ was null" << endl;
    return false; 
  }

  IplImage* fp = patch_->final_patch_;
  if(type_.compare("variance") == 0) {  
    (*result) = new MatrixXf(1,1);

    // -- Get the mean.
    double mean = 0.0;
    for(int r=0; r<fp->height; r++) {
      uchar* ptr = (uchar*)(fp->imageData + r * fp->widthStep);
      for(int c=0; c<fp->width; c++) {
	mean += (double)*ptr;
      }
    }
    mean /= (double)(fp->height * fp->width);
    //cout << "Mean is " << mean << endl;    

    // -- Compute variance.
    double var = 0.0;
    double tmp = 0.0;
    for(int r=0; r<fp->height; r++) {
      uchar* ptr = (uchar*)(fp->imageData + r * fp->widthStep);
      for(int c=0; c<fp->width; c++) {
	tmp = (double)(*ptr) - mean;
	var += tmp * tmp;
	//cout << "tmp " << tmp << " var " << var << endl;
      }
    }
    //cout << "Total pts " << (double)(fp->height * fp->width) << endl;
    var /= (double)(fp->height * fp->width);
    
    (**result)(0,0) = var;
  }

  if(debug) {
    display(**result);
//     cvNamedWindow("fp");
//     cvShowImage("fp", fp);
    commonDebug();
  }

  return true;
}

void PatchStatistic::display(const MatrixXf& result) {
  cout << name_ << " is " << result << endl;
}

/***************************************************************************
***********  ImageDescriptor::SuperpixelStatistic
****************************************************************************/


SuperpixelStatistic::SuperpixelStatistic(int seed_spacing, float scale, SuperpixelStatistic* seg_provider) :
  index_(NULL), seed_spacing_(seed_spacing), scale_(scale), seg_provider_(seg_provider), seg_(NULL)
{
  char buf[100];
  sprintf(buf, "SuperpixelStatistic_seedSpacing%d_scale%g", seed_spacing_, scale_);
  name_ = string(buf);
}

//! Create a mask of 255 for segment number seg, and 0 for everything else.  Useful for histograms.
IplImage* SuperpixelStatistic::createSegmentMask(long label, CvRect* rect) {
  if(!img_ || !seg_) {
    cerr << "Trying to create mask when segmentation does not exist." << endl;
    return NULL;
  }
   
  IplImage* mask = cvCreateImage(cvGetSize(img_), IPL_DEPTH_8U, 1);
  cvZero(mask);

  int max_x = 0;
  int max_y = 0;
  int min_x = img_->width;
  int min_y = img_->height;
  const vector<CvPoint>& l = (*index_)[label];
  vector<CvPoint>::const_iterator lit;
  for(lit = l.begin(); lit != l.end(); lit++) {
    CV_IMAGE_ELEM(mask, uchar, lit->y, lit->x) = 255;
    if(lit->x > max_x)
      max_x = lit->x;
    if(lit->x < min_x)
      min_x = lit->x;
    if(lit->y > max_y)
      max_y = lit->y;
    if(lit->y < min_y)
      min_y = lit->y;
  }

  *rect = cvRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
  
  //Slow.
//   for(int r=0; r<mask->height; r++) {
//     uchar* mask_ptr = (uchar*)(mask->imageData + r * mask->widthStep);
//     long* seg_ptr = (long*)(seg_->imageData + r * seg_->widthStep);
//     for(int c=0; c<mask->width; c++) {
//       if(*seg_ptr == label)
// 	*mask_ptr = 255;
//       else
// 	*mask_ptr = 0;
//       seg_ptr++;
//       mask_ptr++;
//     }
//   }
  return mask;
}

void SuperpixelStatistic::segment(bool debug) {
  ROS_DEBUG_COND(seg_ != NULL, "seg_ is not null, but is being recreated anyway!");
  ROS_DEBUG("Running superpixel segmentation: %s", name_.c_str());
  
  // -- Downsample image.
  IplImage* img_small = cvCreateImage(cvSize(((float)img_->width)*scale_, ((float)img_->height)*scale_), img_->depth, img_->nChannels);
  cvResize(img_, img_small, CV_INTER_AREA);

  // -- Create a grid of seed points.
  IplImage* seg_small = cvCreateImage( cvGetSize(img_small), IPL_DEPTH_32S, 1 );
  cvZero(seg_small);
  long label = 1;
  for(int r=0; r<seg_small->height; r++) {
    long* ptr = (long*)(seg_small->imageData + r * seg_small->widthStep);
    for(int c=0; c<seg_small->width; c++) {
      if(c%seed_spacing_==0 && r%seed_spacing_==0) {
	*ptr = label;
	label++;
      }
      ptr++;
    }
  }

  // -- Compute segmentation.
  double t = (double)cvGetTickCount();
  cvWatershed( img_small, seg_small );
  t = (double)cvGetTickCount() - t;
  inPaintNN(seg_small);

  // -- Check.
  for(int r=0; r<seg_small->height; r++) {
    long* ptr = (long*)(seg_small->imageData + r * seg_small->widthStep);
    for(int c=0; c<seg_small->width; c++) {
      ROS_DEBUG_COND(*ptr >= label, "%ld, max label %ld, for row %d and col %d\n", *ptr, label, r, c);
      ROS_ASSERT(*ptr >= 1);
      ROS_ASSERT(*ptr < label);
    }
  }



  // -- Enlarge segmentation back to size of image.
  seg_ = cvCreateImage( cvGetSize(img_), IPL_DEPTH_32S, 1 );
  cvResize(seg_small, seg_, CV_INTER_NN);


  // -- Check. 
//   IplImage* negs = cvCreateImage( cvGetSize(seg_), 8, 1 );
//   for(int r=0; r<seg_->height; r++) {
//     long* ptr = (long*)(seg_->imageData + r * seg_->widthStep);
//     uchar* negs_ptr = (uchar*)(negs->imageData + r * negs->widthStep);
//     for(int c=0; c<seg_->width; c++) {
//       if(*ptr == -1)
// 	*negs_ptr = 255;
//       else
// 	*negs_ptr = 0;
//     }
//   }

//   cvNamedWindow("negs");
//   cvShowImage("negs", negs);
//   cvWaitKey(0);

  for(int r=0; r<seg_->height; r++) {
    long* ptr = (long*)(seg_->imageData + r * seg_->widthStep);
    for(int c=0; c<seg_->width; c++) {
      ROS_ASSERT(*ptr >= 1);
      ROS_ASSERT(*ptr < label);
    }
  }

  // -- Reserve space for the index.
  int nPixels = seg_->height * seg_->width;
  index_->resize(label+1);
  for(unsigned int i=0; i<index_->size(); i++) {
    (*index_)[i].reserve((nPixels / label) * 2);
  }

  // -- Compute the index.
  for(int r=0; r<seg_->height; r++) {
    long* ptr = (long*)(seg_->imageData + r * seg_->widthStep);
    for(int c=0; c<seg_->width; c++) {
      if(*ptr == -1) {
	ROS_FATAL("Some boundary points have still not been eliminated!");
	continue;
      }
      (*index_)[*ptr].push_back(cvPoint(c,r));
      ptr++;
    }
  }

  if(debug) {
    cout << "Debugging " << name_.c_str() << endl;
    printf( "exec time = %gms\n", t/(cvGetTickFrequency()*1000.) );
  
    // -- Display the results.
    CvMat* color_tab;
    color_tab = cvCreateMat( 1, label, CV_8UC3 );
    CvRNG rng = cvRNG(-1);
    for(int i = 0; i < label; i++ )
      {
	uchar* ptr = color_tab->data.ptr + i*3;
	ptr[0] = (uchar)(cvRandInt(&rng)%180 + 50);
	ptr[1] = (uchar)(cvRandInt(&rng)%180 + 50);
	ptr[2] = (uchar)(cvRandInt(&rng)%180 + 50);
      }
  
    // paint the watershed image
    IplImage* wshed = cvCloneImage( img_ );
    IplImage* img_gray = cvCloneImage( img_ );
    cvZero( wshed );
    IplImage* marker_mask = cvCreateImage( cvGetSize(img_), 8, 1 );
    cvCvtColor( img_, marker_mask, CV_BGR2GRAY );
    cvCvtColor( marker_mask, img_gray, CV_GRAY2BGR );

    for(int i = 0; i < seg_->height; i++ )
      for(int j = 0; j < seg_->width; j++ )
	{
	  int idx = CV_IMAGE_ELEM( seg_, int, i, j );
	  uchar* dst = &CV_IMAGE_ELEM( wshed, uchar, i, j*3 );
	  if( idx == -1 )
	    dst[0] = dst[1] = dst[2] = (uchar)255;
	  else if( idx <= 0 || idx > label )
	    dst[0] = dst[1] = dst[2] = (uchar)0; // should not get here
	  else
	    {
	      uchar* ptr = color_tab->data.ptr + (idx-1)*3;
	      dst[0] = ptr[0]; dst[1] = ptr[1]; dst[2] = ptr[2];
	    }
	}

    cvAddWeighted( wshed, 0.5, img_gray, 0.5, 0, wshed );
    cvNamedWindow(name_.c_str(), CV_WINDOW_AUTOSIZE);
    cvShowImage( name_.c_str(), wshed );

    cvWaitKey(0);
    cvReleaseMat(&color_tab);
    cvReleaseImage(&wshed);
    cvReleaseImage(&marker_mask);
  }

  cvReleaseImage(&img_small);
  cvReleaseImage(&seg_small);
}
  
/***************************************************************************
***********  ImageDescriptor::SuperpixelStatistic::SuperpixelColorHistogram
****************************************************************************/
 
SuperpixelColorHistogram::SuperpixelColorHistogram(int seed_spacing, float scale, int nBins, string type, SuperpixelStatistic* seg_provider, SuperpixelColorHistogram* hsv_provider) :
  SuperpixelStatistic(seed_spacing, scale, seg_provider), hsv_(NULL), hue_(NULL), sat_(NULL), val_(NULL), nBins_(nBins), type_(type), hsv_provider_(hsv_provider), hists_reserved_(false), channel_(NULL)
{
  char buf[100];
  sprintf(buf, "_colorHistogram_type:%s_nBins%d", type_.c_str(), nBins);
  name_.append(buf);

  result_size_ = nBins_;

  max_val_ = 255;
  if(type_.compare("hue") == 0) {
    max_val_ = 180;
  }

  // -- Set up ranges_ for histogram computation.
//   float* range = (float*) malloc(sizeof(float)*2);
//   range[0] = 0;   
//   range[1] = 255;
//   ranges_ = &range;
}

SuperpixelColorHistogram::~SuperpixelColorHistogram() {
  cout << "Destroying SuperpixelColorHistogram" << endl;
  clearPointCache();
  clearImageCache();
}


bool SuperpixelColorHistogram::compute(MatrixXf** result, bool debug) {
  // -- Make sure we have access to a segmentation.
  if(seg_provider_ == NULL && seg_ == NULL) {
    index_ = new vector< vector<CvPoint> >;
    segment(debug);
  }
  else if(seg_provider_ != NULL && seg_ == NULL) {
    seg_ = seg_provider_->seg_;
    index_ = seg_provider_->index_;
  }

  // -- Make sure we have access to HSV image.
  if(hsv_provider_ != NULL && hsv_ == NULL) {
    hsv_ = hsv_provider_->hsv_;
    hue_ = hsv_provider_->hue_;
    sat_ = hsv_provider_->sat_;
    val_ = hsv_provider_->val_;
  }
  else if(hsv_provider_ == NULL && hsv_ == NULL) {
    hsv_ = cvCreateImage( cvGetSize(img_), 8, 3 );
    hue_ = cvCreateImage( cvGetSize(img_), 8, 1 );
    sat_ = cvCreateImage( cvGetSize(img_), 8, 1 );
    val_ = cvCreateImage( cvGetSize(img_), 8, 1 );
    if(type_.compare("hue") == 0 || type_.compare("sat") == 0 || type_.compare("val") == 0)
    cvCvtColor(img_, hsv_, CV_BGR2HSV);
    cvSplit(hsv_, hue_, sat_, val_, 0);
  }

  //  ROS_DEBUG("hsv_ for %s is %p", name_.c_str(), hsv_);
  //  ROS_DEBUG("hsv_provider_ for %s is %p", name_.c_str(), hsv_provider_);

  ROS_ASSERT(hsv_ != NULL);
  ROS_ASSERT(hue_ != NULL);
  ROS_ASSERT(sat_ != NULL);
  ROS_ASSERT(val_ != NULL);


  // -- Get the label at this point.
  int label = CV_IMAGE_ELEM(seg_, long, row_, col_);
  if(label == -1) 
    return false;
// -- Choose which channel we wil use.
  channel_=NULL;
  if(type_.compare("hue") == 0) {
    channel_ = hue_;
  }
  else if(type_.compare("sat") == 0)
    channel_ = sat_;
  else if(type_.compare("val") == 0)
    channel_ = val_;
  else 
    ROS_ERROR("Bad SuperpixelColorHistogram type.");

  // -- Make sure we have access to the histogram data for this point.
  if(!hists_reserved_) {
    histograms_ = vector<Histogram*>(index_->size()+1, NULL);
    histograms_cv_ = vector<CvHistogram*>(index_->size()+1, NULL);
    hists_reserved_ = true;
  }

  // -- Compute the histogram by hand.
  computeHistogram(label);

  // -- Compute the histogram.
  computeHistogramCV(label);

  // -- Copy into result.
  MatrixXf* res = new MatrixXf(result_size_,1);
  for(int i=0; i<nBins_; i++) {
    (*res)(i,0) = cvQueryHistValue_1D(histograms_cv_[label], i);
  }
  *result = res;

  if(debug) {
    // -- Show the mask.
//     cvNamedWindow("Mask");
//     cvShowImage("Mask", mask);
    cout << name_ << endl;;
//     histograms_[label]->printBoundaries();
//     histograms_[label]->print();
//     histograms_[label]->printGraph();
    cout << "cv hist: " << endl;
    for(int i=0; i<nBins_; i++) {
      cout << cvQueryHistValue_1D(histograms_cv_[label], i) << " ";
    }
    cout << endl;
    commonDebug();
  }

  return true;
}

void SuperpixelColorHistogram::computeHistogram(long label) {
  if(histograms_[label] == NULL) {
    Histogram* h = new Histogram(nBins_, 0, max_val_);

    const vector<CvPoint>& l = (*index_)[label];
    vector<CvPoint>::const_iterator lit;
    for(lit = l.begin(); lit != l.end(); lit++) {
      if(!h->insert(CV_IMAGE_ELEM(channel_, uchar, lit->y, lit->x))) {
	ROS_FATAL("Insertion failed for %u ", CV_IMAGE_ELEM(channel_, uchar, lit->y, lit->x));
	h->printBoundaries();
	ROS_BREAK();
      }
    }
      
    h->normalize();
    histograms_[label] = h;
  }
}

void SuperpixelColorHistogram::computeHistogramCV(long label) {
  if(!hue_) 
    ROS_FATAL("Trying to compute cv hist when hue_ is null");

  if(histograms_cv_[label] == NULL) {
    //ROS_DEBUG("Computing hist for %s for label %ld", name_.c_str(), label);
    float range[] = {0, 180}; //hsv
    float* ranges[] = {range};
    int sizes[] = {nBins_};
    CvHistogram* hist = cvCreateHist(1, sizes, CV_HIST_ARRAY, ranges, 1);
    //cout << "nBins " << nBins_ << "&hue_ " << &hue_ << "hue_ " << hue_ << "hist " << hist << "ranges " << ranges << "mask " << mask << endl;
    CvRect rect;
    IplImage* mask = createSegmentMask(label, &rect);
//     cvNamedWindow("m");
//     cvShowImage("m", mask);
//     cvWaitKey(0);
    //For efficiency, set the ROI's.
    cvSetImageROI(hue_, rect);
    cvSetImageROI(mask, rect);
    cvCalcHist(&hue_, hist, 0, mask);
    cvReleaseImage(&mask);
    cvResetImageROI(hue_);

    cvNormalizeHist(hist, 1);
    histograms_cv_[label] = hist;
  }
  else {
    //ROS_DEBUG("hist already cached for %s for label %ld", name_.c_str(), label);
  }
}

//! Release seg_, etc., if this object is the one that computes them; otherwise, just nullify the pointers.
void SuperpixelColorHistogram::clearImageCache() {
  ROS_DEBUG("Clearing the image cache for %s", name_.c_str());
  
  if(hsv_provider_ == NULL) {
    cvReleaseImage(&hsv_);
    cvReleaseImage(&hue_);
    cvReleaseImage(&sat_);
    cvReleaseImage(&val_);
  }
  else {
    hsv_ = NULL;
    hue_ = NULL;
    sat_ = NULL;
    val_ = NULL;
  }

  // -- Clean up index and seg_.
  if(seg_provider_ == NULL) {
    cvReleaseImage(&seg_);
    if(index_) {
      cout << "Deleting index in " << name_ << endl;
      delete index_;
    }
  }
  else {
    seg_ = NULL;
    index_ = NULL;
  }


  // -- Clean up histograms.
  for(unsigned int i=0; i<histograms_.size(); i++) {
    if(histograms_[i])
      delete histograms_[i];
  }
  histograms_.clear();

  // -- Clean up cv histograms.
  for(unsigned int i=0; i<histograms_cv_.size(); i++) {
    if(histograms_cv_[i])
      cvReleaseHist(&histograms_cv_[i]);
  }
  histograms_.clear();


  hists_reserved_ = false;
}


 
/***************************************************************************
***********  ImageDescriptor::IntegralImageDescriptor
****************************************************************************/
 

IntegralImageDescriptor::IntegralImageDescriptor(IntegralImageDescriptor* ii_provider) 
  : ImageDescriptor(), ii_(NULL), ii_tilt_(NULL), gray_(NULL), ii_provider_(ii_provider)
{
  char buf[100];
  sprintf(buf, "IntegralImageDescriptor");
  name_ = string(buf);
}

IntegralImageDescriptor::~IntegralImageDescriptor() {
  if(!ii_provider_) {
    if(ii_)
      cvReleaseImage(&ii_);
    if(ii_tilt_)
      cvReleaseImage(&ii_tilt_);
    if(gray_)
      cvReleaseImage(&gray_);
  }
}

void IntegralImageDescriptor::integrate() {
  
  if(!img_)
    ROS_FATAL("Trying to compute integral image of null image.");
  if(!gray_) {
    gray_ = cvCreateImage(cvGetSize(img_), IPL_DEPTH_8U, 1);
    cvCvtColor(img_, gray_, CV_BGR2GRAY);
  }

  // TODO: Don't reallocate for every image.
  CvSize sz = cvGetSize(img_);
  sz.height++;
  sz.width++;
  ii_ = cvCreateImage(sz, IPL_DEPTH_32S, 1);
  ii_tilt_ = cvCreateImage(sz, IPL_DEPTH_32S, 1);
  cvIntegral(gray_, ii_, NULL, ii_tilt_);
}

void IntegralImageDescriptor::clearImageCache() {
  if(ii_) {
    if(!ii_provider_) {
      cvReleaseImage(&ii_);
      cvReleaseImage(&ii_tilt_);
      cvReleaseImage(&gray_);
    }
    else {
      ii_ = NULL;
      ii_tilt_ = NULL;
      gray_ = NULL;
    }
  }
} 

bool IntegralImageDescriptor::integrateRect(float* result, int row_offset, int col_offset, int half_height, int half_width) {
  // -- Check that we have an integral image.
  if(!ii_) {
    return false;
    ROS_WARN("No integral image.");
  }

  // -- Check bounds.
  int ul_x = col_ + col_offset - half_width; 
  int ul_y = row_ + row_offset - half_height;
  int ll_x = ul_x;
  int ll_y = row_ + row_offset + half_height;
  int ur_x = col_ + col_offset + half_width;
  int ur_y = ul_y;
  int lr_x = ur_x;
  int lr_y = ll_y;
  
  if(ul_x < 0 || ul_y < 0 || lr_y >= img_->height || lr_x >= img_->width)  {
    //    ROS_DEBUG("Out of bounds. %d %d, %d %d", ul_x, ul_y, lr_x, lr_y);
    return false;
  }

  // -- Compute the rectangle sum.  +1 is because of ii_ dfn
  *result =   CV_IMAGE_ELEM(ii_, int, ul_y    , ul_x    ) \
            + CV_IMAGE_ELEM(ii_, int, lr_y + 1, lr_x + 1) \
            - CV_IMAGE_ELEM(ii_, int, ur_y    , ur_x + 1) \
            - CV_IMAGE_ELEM(ii_, int, ll_y + 1, ll_x    );

  // -- Check that it's right.  
//   int check = 0;
//   for(int r=ul_y; r<=ll_y; r++) {
//     for(int c=ul_x; c<=ur_x; c++) {
//       check += CV_IMAGE_ELEM(gray_, uchar, r, c);
//     }
//   }
//   if(abs(check - *result) > 0) {
//     cout << check - *result << " difference for computing at row " << row_ << " and col " << col_ << endl;
//     ROS_ASSERT(0);
//   }

  return true;
}

/***************************************************************************
***********  ImageDescriptor::IntegralImageDescriptor::IntegralImageTexture
****************************************************************************/
IntegralImageTexture::IntegralImageTexture(int scale, IntegralImageDescriptor* ii_provider)
  : IntegralImageDescriptor(ii_provider), scale_(scale) 
{
  char buf[100];
  sprintf(buf, "_IntegralImageTexture_scale%d", scale_);
  name_.append(buf);

  //result_size_ = 17; //TextonBoost-like texture descriptors.
  result_size_ = 21;
}

// How do I make this get called when I am calling setImage on a IntegralImageDescriptor* ?
// void IntegralImageTexture::setImage(IplImage* img) {
//   cvNamedWindow("x");
//   cvShowImage("x", img);
//   cvWaitKey(0);
//   cvGetSize(img);
//   ImageDescriptor::setImage(img);
//   if(!img_)
//     ROS_FATAL("Img is NULL in %s", name_.c_str());
 
//   ROS_DEBUG("Calling IntegralImageTexture's setImage");
// }

bool IntegralImageTexture::compute(Eigen::MatrixXf** result, bool debug) {
  if(!ii_)
    integrate();
  
  int szs = 8;
  vector<float> val(8,0);
  vector<float> area(8,0);
  
  bool success=true;
  MatrixXf* res = new MatrixXf(result_size_, 1);
  int ctr = 0;

  // -- 8 Center-surround
  success &= integrateRect(&val[0], 0, 0, 1, 1);
  area[0] = 9;
  for(int i=1; i<=szs; i++) {
    success &= integrateRect(&val[i], 0, 0, i+1, i+1); 
    area[i] = pow((double)(2*(i+1)+1), 2);
    (*res)(ctr,0) = (val[i-1] - val[i]) / (area[i]-area[i-1]) + val[i-1] / area[i-1];
    ctr++;
  }
  
  //5 finer grained center-surround.
  for(unsigned int i=3; i<val.size(); i++) {
    (*res)(ctr,0) = (2./3.) * val[i-3] / area[i-3] + (1./3.) * (val[i-2] - val[i-3]) / (area[i-2] - area[i-3]) \
      - (1./3.) * (val[i-1] - val[i-2]) / (area[i-1] - area[i-2]) - (2./3.) * (val[i] - val[i-1]) / (area[i] - area[i-1]);
    ctr++;
  }

  // -- Gabor Vert
  success &= integrateRect(&val[0], 0, 1, 2, 0); 
  success &= integrateRect(&val[1], 0, -1, 2, 0); 
  (*res)(ctr,0) = (val[0] - val[1]) / 5.;
  ctr++;

  success &= integrateRect(&val[0], 0, 1, 5, 0); 
  success &= integrateRect(&val[1], 0, -1, 5, 0); 
  (*res)(ctr,0) = (val[0] - val[1]) / 11.;
  ctr++;

  success &= integrateRect(&val[0], 0, 2, 2, 1); 
  success &= integrateRect(&val[1], 0, -2, 2, 1); 
  (*res)(ctr,0) = (val[0] - val[1]) / 15;
  ctr++;

  success &= integrateRect(&val[0], 0, 2, 5, 1); 
  success &= integrateRect(&val[1], 0, -2, 5, 1); 
  (*res)(ctr,0) = (val[0] - val[1]) / 33.;
  ctr++;

  // -- Horiz
  success &= integrateRect(&val[0], 1, 0, 0, 2); 
  success &= integrateRect(&val[1], -1, 0, 0, 2); 
  (*res)(ctr,0) = (val[0] - val[1]) / 5.;
  ctr++;
  
  success &= integrateRect(&val[0], 1, 0, 0, 5); 
  success &= integrateRect(&val[1], -1, 0, 0, 5); 
  (*res)(ctr,0) = (val[0] - val[1]) / 11.;
  ctr++;
  
  success &= integrateRect(&val[0], 2, 0, 1, 2); 
  success &= integrateRect(&val[1], -2, 0, 1, 2); 
  (*res)(ctr,0) = (val[0] - val[1]) / 15.;
  ctr++;

  success &= integrateRect(&val[0], 2, 0, 1, 5); 
  success &= integrateRect(&val[1], -2, 0, 1, 5);
  (*res)(ctr,0) = (val[0] - val[1]) / 33.;
  ctr++;


  if(!success) {
    delete res;
    return false;
  }


  // -- Check that the scaling is between -255 and 255.
//   static vector<float> max(result_size_, -1e20);
//   static vector<float> min(result_size_, 1e20);
//   for(unsigned int i=0; i<result_size_; i++) {
//     assert((*res)(i,0) <= 255 && (*res)(i,0) >= -255);
//     if((*res)(i,0) > max[i])
//       max[i] = (*res)(i,0);
//     if((*res)(i,0) < min[i])
//       min[i] = (*res)(i,0);
//   }
//   static int count = 0;
//   count++;
//   if(count % 1000 == 0) {
//     cout << "Max: ";
//     for(unsigned int i=0; i<result_size_; i++) {
//       cout << max[i] << " ";
//     }
//     cout << endl;
//     cout << "Min: ";
//     for(unsigned int i=0; i<result_size_; i++) {
//       cout << min[i] << " ";
//     }
//     cout << endl;
//   }


  *result = res;
  if(debug) {
    cout << name_ << " descriptor: " << res->transpose() << endl;
    commonDebug();
  }

  return true;
}
  


 
/***************************************************************************
***********  Histogram
****************************************************************************/
 

Histogram::Histogram(int nBins, float min, float max) 
  : nInsertions_(0), nBins_(nBins), min_(min), max_(max)
{
  bin_size_ = (max_ - min_) / (float)nBins_;
  boundaries_.reserve(nBins_+1);

  float b = min_;
  while(b <= max_) {
    boundaries_.push_back(b);
    b += bin_size_;
  } 
  boundaries_.push_back(max);

  bins_.reserve(nBins_);
  for(int i=0; i<nBins; i++) {
    bins_.push_back(0);
  }

//   cout << "Boundaries: ";
//   for(unsigned int i=0; i<boundaries_.size(); i++) {
//     cout << boundaries_[i] << " ";
//   }
//   cout << endl;
}

//! Temporary, slow version.
bool Histogram::insert(float val) {
  nInsertions_++;
  for(unsigned int i=0; i<boundaries_.size() - 1; i++) {
    if (boundaries_[i] <= val && boundaries_[i+1] + .001 >= val) {
      bins_[i]++;
      return true;
    }
  }
  return false;
}

void Histogram::normalize() {
  float total = 0;
  for(unsigned int i=0; i<bins_.size(); i++) {
    total += bins_[i];
  }
  if(total == 0) {
    ROS_WARN("Empty histogram!");
    return;
  }
  for(unsigned int i=0; i<bins_.size(); i++) {
    bins_[i] /= total;
  }
}

void Histogram::print() {
  cout << "Histogram (" << nInsertions_ << " insertions): " << endl;
  for(unsigned int i=0; i<bins_.size(); i++) {
    cout << bins_[i] << " ";
  }
  cout << endl;
}

void Histogram::printGraph() {
  Histogram h2 = *this;
  h2.normalize();

  cout << "Histogram (" << nInsertions_ << " insertions) graph: " << endl;
  for(float row=1; row >= 0; row=row-.1) {
    for(unsigned int i=0; i<h2.bins_.size(); i++) {
      if(h2.bins_[i] >= row)
	cout << "*";
      else
	cout << " ";
    }
    cout << endl;
  }
  for(unsigned int i=0; i<h2.bins_.size(); i++) {
    cout << "-";
  }
  cout << endl;
}


void Histogram::printBoundaries() {
  cout << "Histogram Boundaries: " << endl;
  for(unsigned int i=0; i<boundaries_.size(); i++) {
    cout << boundaries_[i] << " ";
  }
  cout << endl;
}

void Histogram::clear() {
  for(unsigned int i=0; i<bins_.size(); i++) {
    bins_[i] = 0;
  }
  nInsertions_ = 0;
}
  
  

      
    
  
  


/***************************************************************************
***********  ImageDescriptor::Patch::EdgePatch
****************************************************************************/

/*

EdgePatch::compute(MatrixXf** result, bool debug) {
  Patch::compute(IplImage* img_, row_, col_, result, debug);
  IplImage* gray = cvCreateImage(cvGetSize(img_), IPL_DEPTH_8U, 1);
  IplImage* detail_edge = cvCloneImage(gray);
  cvCvtColor(img_, gray, CV_BGR2GRAY);
  cvCanny(gray, detail_edge, thresh1_, thresh2_);
  cvNamedWindow("detail_edge");
  cvShowImage("detail_edge", detail_edge);
  
  cvResize(detail_edge, final_patch_, CV_INTER_AREA);
  cvReleaseImage(&gray);
  cvReleaseImage(&detail_edge);
  
  // -- Convert ipl to Newmat.
  int idx=0;
  for(int r=0; r<final_patch_->height; r++) {
    uchar* ptr = (uchar*)(final_patch_->imageData + r * final_patch_->widthStep);
    for(int c=0; c<final_patch_->width; c++) {
      (*res)(idx+1, 1) = *ptr;
      ptr++;
      idx++;
    }
  }
  result = res;

  // -- Set mean to 0, variance to 1 if appropriate and desired.
  if(whiten_)
    whiten(result);
  
  // -- Display for debugging.
  if(debug) {
    cout << name_ << " dump: ";
    cout << (*result).t() << endl;
      
    IplImage* final_patch_rescaled = cvCreateImage(cvSize(500,500), IPL_DEPTH_8U, 1);
    cvResize(final_patch_, final_patch_rescaled, CV_INTER_NN);
    cvNamedWindow(name_.c_str());
    cvShowImage(name_.c_str(), final_patch_rescaled);
    commonDebug(img_, row_, col_);

    cvReleaseImage(&final_patch_rescaled);
  }
   
  // -- Clean up.
  cvResetImageROI(img_);
  return true;
}

*/


/****************************************************************************
*************  ImageDescriptor::Hog
****************************************************************************/

/*

Hog::Hog(Patch* patch) {
  int blocksz = 16;
  p_ = patch->final_patch_;
  ROS_DEBUG_COND((fp->height % blocksz != 0 || fp->width % blocksz != 0), "Hog patch must be divisible by blocksize.");
  cvHog = HOGDescriptor(cvGetSize(fp), cvSize(blocksz, blocksz), cvSize(8,8), cvSize(8,8), 1);
}

bool Hog::compute(MatrixXf** result, bool debug);
*/


