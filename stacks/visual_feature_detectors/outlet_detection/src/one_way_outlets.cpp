/*
 *  one_way_outlets.cpp
 *  online_patch
 *
 *  Created by Victor  Eruhimov on 5/16/09.
 *  Copyright 2009 Argus Corp. All rights reserved.
 *
 */

#include "outlet_detection/one_way_outlets.h"
#include "outlet_detection/one_way_descriptor.h"
#include "outlet_detection/constellation.h"

#include <highgui.h>

void drawLine(IplImage* image, CvPoint p1, CvPoint p2, CvScalar color, int thickness)
{
    if(p1.x < 0 || p1.y < 0 || p2.y < 0 || p2.y < 0) return;
    
    cvLine(image, p1, p2, color, thickness);
}

void detect_outlets_2x1_one_way(IplImage* test_image, const CvOneWayDescriptorBase* descriptors, 
                                vector<feature_t>& holes, const char* output_path, const char* output_filename)
{
    
    IplImage* image = cvCreateImage(cvSize(test_image->width, test_image->height), IPL_DEPTH_8U, 3);
    cvCvtColor(test_image, image, CV_GRAY2RGB);
    IplImage* image1 = cvCloneImage(image);
    
    int64 time1 = cvGetTickCount();
    
    vector<feature_t> features;
    GetHoleFeatures(test_image, features);
    
    int64 time2 = cvGetTickCount();
    
    printf("Found %d test features, time elapsed: %f\n", (int)features.size(), float(time2 - time1)/cvGetTickFrequency()*1e-6);
    
    IplImage* test_image_features = cvCreateImage(cvSize(test_image->width, test_image->height), IPL_DEPTH_8U, 3);
    cvCvtColor(test_image, test_image_features, CV_GRAY2RGB);
    DrawFeatures(test_image_features, features);
    
    vector<feature_t> hole_candidates;
    int patch_width = descriptors->GetPatchSize().width;
    int patch_height = descriptors->GetPatchSize().height; 
    for(int i = 0; i < (int)features.size(); i++)
    {
        CvPoint center = features[i].center;
        float scale = features[i].scale;
        
        CvRect roi = cvRect(center.x - patch_width/2, center.y - patch_height/2, patch_width, patch_height);
        cvSetImageROI(test_image, roi);
        roi = cvGetImageROI(test_image);
        if(roi.width != patch_width || roi.height != patch_height)
        {
            continue;
        }
        
        if(abs(center.x - 1129) < 10 && abs(center.y - 1325) < 10)
        {
            int w = 1;
        }
        
        int desc_idx = -1;
        int pose_idx = -1;
        float distance = 0;
        descriptors->FindDescriptor(test_image, desc_idx, pose_idx, distance);
        
        CvPoint center_new = descriptors->GetDescriptor(desc_idx)->GetCenter();
        CvScalar color = descriptors->IsDescriptorObject(desc_idx) ? CV_RGB(0, 255, 0) : CV_RGB(255, 0, 0);
        int part_idx = descriptors->GetDescriptorPart(desc_idx);
        if(part_idx >= 0 && part_idx < 4)
        {
            color = CV_RGB(255, 255, 0);
        }
        
        if(part_idx == 5 || part_idx == 6)
        {
            color = CV_RGB(0, 255, 255);
        }
        
        if(part_idx >= 0)
        {
            feature_t candidate = features[i];
            if(part_idx < 4) candidate.part_id = 0;
                else candidate.part_id = 1;
            hole_candidates.push_back(candidate);                    
        }
        
        cvCircle(image, center, scale, color, 2);
        
        cvResetImageROI(test_image);
        
#if 0
        IplImage* image1 = cvCreateImage(cvSize(train_image->width, train_image->height), IPL_DEPTH_8U, 3);
        cvCvtColor(train_image, image1, CV_GRAY2RGB);
        IplImage* image2 = cvCreateImage(cvSize(test_image->width, test_image->height), IPL_DEPTH_8U, 3);
        cvCvtColor(test_image, image2, CV_GRAY2RGB);
        
        cvCircle(image1, center_new, 20, cvScalar(255, 0, 0), 2);
        cvCircle(image2, center, 20, cvScalar(255, 0, 0), 2);
#endif   
        
        //printf("Old center: %d,%d; new center: %d,%d\n", center_new.x, center_new.y, center.x, center.y);
        CvAffinePose pose = descriptors->GetDescriptor(desc_idx)->GetPose(pose_idx);
        //            printf("i = %d, pose: %f,%f,%f,%f\n", i, pose.phi, pose.theta, pose.lambda1, pose.lambda2);
        //            printf("Distance = %f\n\n", distance);
        
#if 0
        cvNamedWindow("1", 1);
        cvShowImage("1", image1);
        
        cvNamedWindow("2", 1);
        cvShowImage("2", image2);
        cvWaitKey(0);
        cvReleaseImage(&image1);
        cvReleaseImage(&image2);
#endif   
    }
    
    int64 time3 = cvGetTickCount();
    printf("Features matched. Time elapsed: %f\n", float(time3 - time2)/cvGetTickFrequency()*1e-6);       
    
    //        printf("%d features before filtering\n", (int)hole_candidates.size());
    vector<feature_t> hole_candidates_filtered;
    float dist = calc_set_std(descriptors->GetTrainFeatures());
    FilterOutletFeatures(hole_candidates, hole_candidates_filtered, dist*4);
    hole_candidates = hole_candidates_filtered;
    //        printf("Set size is %f\n", dist);
    //        printf("%d features after filtering\n", (int)hole_candidates.size());
    
    // clustering
    vector<feature_t> clusters;
    ClusterOutletFeatures(hole_candidates, clusters, dist*4);
    //        float min_error = 0;
    //        vector<feature_t> min_features;
    
    vector<int> indices;
    
#if defined(_HOMOGRAPHY)
    CvMat* homography = cvCreateMat(3, 3, CV_32FC1);
#else
    CvMat* homography = cvCreateMat(2, 3, CV_32FC1);
#endif //_HOMOGRAPHY
    
    for(int k = 0; k < (int)clusters.size(); k++)
    {
        vector<feature_t> clustered_features;
        SelectNeighborFeatures(hole_candidates, clusters[k].center, clustered_features, dist*4);
        
        DetectObjectConstellation(descriptors->GetTrainFeatures(), clustered_features, homography, indices);
        
        // print statistics
        int parts = 0;
        for(int i = 0; i < (int)indices.size(); i++) parts += (indices[i] >= 0);
#if 0
        printf("Found %d parts: ", parts);
        vector<int> indices_sort = indices;
        sort(indices_sort.begin(), indices_sort.end());
        for(int i = 0; i < (int)indices_sort.size(); i++) if(indices_sort[i] >= 0) printf("%d", indices_sort[i]);
        printf("\n");
#endif
        
        // infer missing objects 
        if(parts > 0)
        {
            holes.clear();
            InferMissingObjects(descriptors->GetTrainFeatures(), clustered_features, homography, indices, holes);
        }
    }
    
    cvReleaseMat(&homography);
    
#if 0
    holes.resize(6);
    for(int i = 0; i < indices.size(); i++)
    {
        if(indices[i] == -1) continue;
        holes[indices[i]] = hole_candidates[i];
    }
#endif
    
    int64 time4 = cvGetTickCount();
    printf("Object detection completed. Time elapsed: %f\n", float(time4 - time3)/cvGetTickFrequency()*1e-6);
    printf("Total time elapsed: %f\n", float(time4 - time1)/cvGetTickFrequency()*1e-6);
    
    IplImage* image2 = cvCloneImage(image1);
    CvScalar color_parts[] = {CV_RGB(255, 255, 0), CV_RGB(0, 255, 255)};
    for(int i = 0; i < (int)hole_candidates.size(); i++)
    {
        cvCircle(image2, hole_candidates[i].center, hole_candidates[i].scale, color_parts[hole_candidates[i].part_id], 2);
    }
    
    CvScalar color[] = {CV_RGB(255, 255, 0), CV_RGB(255, 255, 0), CV_RGB(128, 128, 0), 
    CV_RGB(128, 128, 0), CV_RGB(0, 255, 255), CV_RGB(0, 128, 128)};
    for(int i = 0; i < (int)holes.size(); i++)
    {
        //CvScalar color = i < 4 ? CV_RGB(255, 255, 0) : CV_RGB(0, 255, 255);
        cvCircle(image1, holes[i].center, holes[i].scale, color[i], 2);
    }
    
    if(holes.size() >= 6)
    {
        drawLine(image1, holes[0].center, holes[1].center, CV_RGB(255, 0, 0), 3);
        drawLine(image1, holes[1].center, holes[4].center, CV_RGB(255, 0, 0), 3);
        drawLine(image1, holes[4].center, holes[0].center, CV_RGB(255, 0, 0), 3);
        
        drawLine(image1, holes[2].center, holes[3].center, CV_RGB(255, 0, 0), 3);
        drawLine(image1, holes[3].center, holes[5].center, CV_RGB(255, 0, 0), 3);
        drawLine(image1, holes[5].center, holes[2].center, CV_RGB(255, 0, 0), 3);
    }
    
#if defined(_VERBOSE)
    char test_image_filename[1024];
    sprintf(test_image_filename, "%s/features/%s", output_path, output_filename);
    cvSaveImage(test_image_filename, image);
    
    sprintf(test_image_filename, "%s/outlets/%s", output_path, output_filename);
    cvSaveImage(test_image_filename, image1);
    
    sprintf(test_image_filename, "%s/features_filtered/%s", output_path, output_filename);
    cvSaveImage(test_image_filename, image2);
#endif //_VERBOSE
        
    cvReleaseImage(&image);
    cvReleaseImage(&image1);
    cvReleaseImage(&image2);
}