////////////////////////////////////////////////////////////////////////////////////////////////////
//
//    Sample codes for UTH CAD Challenge
//
//	      vessel_segmentation.cpp : Vessel segmentation from MRA images
//
//    [CAUTION] The sample codes are permitted to use only for research purposes.
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#define _CRT_SECURE_NO_DEPRECATE

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "VOL.h"
#include "VOLbinary.h"
#include "VOLfilter.h"
#include "LibCircusCS.h"

#include "vessel_segmentation.h"

#define  SEED_VOI_HALF_SIZE_XY   25.0	// [mm]

////////////////////////////////////////////////////////////////////////////////////////////////////

void
getStaticticsOfBrainLevel(VOL_RAWVOLUMEDATA* volume, float* brain_mean, float* brain_sigma)
{
	short***		voxels=(short***)volume->array4D[0];
	int				x0, y0, z0;
	int				x_end, y_end, z_end;
	unsigned long	histogram[MAX_SEARCH+1];
	float			average, stddev, count;

	x0 = volume->matrixSize->width  / 4;
	y0 = volume->matrixSize->height / 4;
	z0 = volume->matrixSize->depth  / 2;
	x_end = volume->matrixSize->width  / 4 * 3;
	y_end = volume->matrixSize->height / 4 * 3;
	z_end = volume->matrixSize->depth  / 2 + 1;

	for(int i=0; i<=MAX_SEARCH; i++)	histogram[i] = 0;

	for(int k = z0; k < z_end; k++)
	for(int j = y0; j < y_end; j++)
	for(int i = x0; i < x_end; i++)
	{
		int	value = (int)voxels[k][j][i];

		if(value >= MIN_SEARCH && value <= MAX_SEARCH)
		{
			histogram[value] += 1;
		}
	}
		
	average = stddev = count = 0;

	for(int i = MIN_SEARCH; i <= MAX_SEARCH; i++)
	{
		average += (float)(histogram[i]*i);
		count   += (float)histogram[i];
		stddev  += (float)histogram[i]*(float)(i*i);
	}

	average /= count;

	stddev -= (count*average*average);
	stddev /= (count-1.0f);
	stddev = (float)sqrt(stddev);

	*brain_mean  = average;
	*brain_sigma = stddev;

	return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////

VOL_RAWVOLUMEDATA* getVesselMask(
	VOL_RAWVOLUMEDATA* original,
	VOL_SIZE3D* voxel_size,
	float brain_mean,
	float brain_sigma,
	char* log_file_name)
{

	VOL_RAWVOLUMEDATA	*mask = VOL_DuplicateRawVolumeData(original);
	unsigned int		total_voxels;
	char				buffer[512];

	VOL_VOLVALUERANGE threshold_range;
	threshold_range.min.uint8 = 1;
	threshold_range.max.uint8 = 255;

	// Region growing
	{
		// Make seed point for region growing
		int voi_half_size_xy = (int)(SEED_VOI_HALF_SIZE_XY / voxel_size->width); 

		int x0 = max(original->matrixSize->width  / 2 - voi_half_size_xy, 0);
		int y0 = max(original->matrixSize->height / 2 - voi_half_size_xy, 0);
		int z0 = original->matrixSize->depth * 2 / 5;
		int x_end = min(original->matrixSize->width  / 2 + voi_half_size_xy,
						original->matrixSize->width);
		int y_end = min(original->matrixSize->height / 2 + voi_half_size_xy,
						original->matrixSize->height);
		int z_end = original->matrixSize->depth * 4 / 5;

		short*** src_data = (short***)original->array4D[0];
		short    seed_th  = (short)(brain_mean + brain_sigma * 3.0f);
		int      seed_num = 0;

		sprintf(buffer, "--- Threshold for seed: %d", seed_th);
		CircusCS_AppendLogFile(log_file_name, buffer);

		for(int k = z0; k < z_end; k++)
		for(int j = y0; j < y_end; j++)
		for(int i = x0; i < x_end; i++)
		{
			if(src_data[k][j][i] > seed_th)	seed_num++;
		}

		VOL_LOCATIONARRAY* seed_array=VOL_NewLocationArray(seed_num);

		for(int k = z0; k < z_end; k++)
		for(int j = y0; j < y_end; j++)
		for(int i = x0; i < x_end; i++)
		{
			if(src_data[k][j][i] > seed_th)
			{
				VOL_AddLocationArrayElement(seed_array, VOL_NewIntVector3D(i, j, k));
			}
		}

		VOL_VOLVALUERANGE	growing_range;
		growing_range.min.sint16 = (short)(brain_mean + brain_sigma * 2.5f);
		growing_range.max.sint16 = VOL_MAX_OF_SINT16;

		sprintf(buffer, "--- Threshold for region growing: %d", growing_range.min.sint16);
		CircusCS_AppendLogFile(log_file_name, buffer);
			
		int num_voxels = VOL_RegionGrowingSeededByPoints(
							mask,
							0,
							&growing_range,
							VOL_NEIGHBOURTYPE_26,
							seed_array);
		
		VOL_ConvertVoxelUnit(mask, 0, VOL_VALUEUNIT_UINT8, NULL, NULL, VOL_CONVERTUNIT_TYPE_DIRECT);
	}

	// Counting the number of extracted voxels 
	total_voxels = VOL_ThresholdingMinMax(mask, 0, &threshold_range);

	// Labeling and vessel selection
	{
		unsigned int  component_ids[8];
		int			  extracted_cc_cnt = 0;
		VOL_VECTOR3D  centor_of_gravity;

		int cmps_num = VOL_ConnectedComponentAnalysis(mask,0,VOL_NEIGHBOURTYPE_26);
		
		VOL_COMPONENTDATA*	cc_data = VOL_NewComponentData(mask, 0, cmps_num);

		sprintf(buffer, "Vessel shape: %d components", cmps_num);
		CircusCS_AppendLogFile(log_file_name, buffer);

		if(cmps_num == 0 || cc_data == NULL)	return mask;

		VOL_SortComponentsByProperty( cc_data, VOL_CC_PROPERTY_ID_VOXELCOUNT );

		for(int i = 1; i <= 8 && i <= cmps_num; i++)
		{
			float volume_percentage;

			VOL_GetCenterOfGravityOfComponent( cc_data, (unsigned long)i, &centor_of_gravity );

			volume_percentage = (float)cc_data->nVoxelsOfComponents[i]/(float)total_voxels*100.0f;
			
			sprintf(buffer, "  %d: volume=%3.2f %%, G =(%.1f, %.1f, %.1f)",
				i,
				volume_percentage,
				centor_of_gravity.x,
				centor_of_gravity.y,
				centor_of_gravity.z);

			if( volume_percentage >= 5.0f )
			{
				component_ids[extracted_cc_cnt++] = i;
				strcat(buffer," <===");
			}
			CircusCS_AppendLogFile(log_file_name, buffer);
		}

		// for the case
		if(extracted_cc_cnt == 0)
		{
			CircusCS_AppendLogFile(log_file_name, "Warning: segmentation failed?");

			component_ids[0] = 1;
			extracted_cc_cnt = 1;
		}

		VOL_ExtractComponents(cc_data, extracted_cc_cnt, component_ids);
		VOL_DeleteComponentData(cc_data);

		VOL_ConvertVoxelUnit(mask, 0, VOL_VALUEUNIT_UINT8, NULL, NULL, VOL_CONVERTUNIT_TYPE_DIRECT);
	}

	// just make binary
	VOL_ThresholdingMinMax(mask, 0, &threshold_range);

	// Final masking to obtain vessel signal volume
	VOL_DilateBinaryVolumeBySphere(mask, 0, 1.9f);
	VOL_ErodeBinaryVolumeBySphere(mask, 0, 1.9f);

	VOL_RemoveCavity(mask, 0);

	return mask;
}
