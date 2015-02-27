/*=========================================================================
 *
 *  Copyright RTK Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#include "rtkCudaConjugateGradientImageFilter_4f.h"
#include "rtkCudaConjugateGradientImageFilter_4f.hcu"
#include "rtkCudaConstantVolumeSeriesSource.h"

#include <itkMacro.h>

#include <itkImageFileWriter.h>

rtk::CudaConjugateGradientImageFilter_4f
::CudaConjugateGradientImageFilter_4f()
{
}

void
rtk::CudaConjugateGradientImageFilter_4f
::GPUGenerateData()
{
  int size[4];

  for (int i=0; i<4; i++)
    {
    size[i] = this->GetOutput()->GetLargestPossibleRegion().GetSize()[i];
    }

  // Initialization
  typedef itk::ImageFileWriter< itk::CudaImage<float, 4> > WriterType;
  typename WriterType::Pointer writer = WriterType::New();
  writer->SetInput(this->GetX());
  writer->SetFileName("x_0.mha");
  writer->Update();

  // Copy the input to the output (X_0 = input)
  float *pin = *(float**)( this->GetX()->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pX = *(float**)( this->GetOutput()->GetCudaDataManager()->GetGPUBufferPointer() );

  // On GPU, initialize the output to the input
  CUDA_copy_X_4f(size, pin, pX);

  this->m_A->SetX(this->GetX());
  this->m_A->Update();

  // Create a source to generate the intermediate images
  rtk::CudaConstantVolumeSeriesSource::Pointer source = rtk::CudaConstantVolumeSeriesSource::New();
  source->SetInformationFromImage(this->GetOutput());
  source->SetConstant(0);

  // Initialize P_k
  source->Update();
  itk::CudaImage<float, 4>::Pointer P_k = source->GetOutput();
  P_k->DisconnectPipeline();

  // Initialize R_k
  source->Update();
  itk::CudaImage<float, 4>::Pointer R_k = source->GetOutput();
  R_k->DisconnectPipeline();

  // Initialize AOut
  itk::CudaImage<float, 4>::Pointer AOut = this->m_A->GetOutput();
  AOut->DisconnectPipeline();

  float *pR = *(float**)( R_k->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pB = *(float**)( this->GetB()->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pAOut = *(float**)( AOut->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pP = *(float**)( P_k->GetCudaDataManager()->GetGPUBufferPointer() );

  // Compute, on GPU, R_zero = P_zero = this->GetB() - this->m_A->GetOutput()
  CUDA_subtract_4f(size, pB, pAOut, pP, pR);

  writer->SetInput(P_k);
  writer->SetFileName("p_0.mha");
  writer->Update();

  for (int iter=0; iter<m_NumberOfIterations; iter++)
    {
    // Compute A * P_k
    this->m_A->SetX(P_k);
    this->m_A->Update();
    AOut = this->m_A->GetOutput();
    AOut->DisconnectPipeline();

    float *pX = *(float**)( this->GetOutput()->GetCudaDataManager()->GetGPUBufferPointer() );
    float *pR = *(float**)( R_k->GetCudaDataManager()->GetGPUBufferPointer() );
    float *pP = *(float**)( P_k->GetCudaDataManager()->GetGPUBufferPointer() );
    float *pAOut = *(float**)( AOut->GetCudaDataManager()->GetGPUBufferPointer() );

    // Compute, on GPU, alpha_k (only on GPU), X_k+1 (output), R_k+1, beta_k (only on GPU), P_k+1
    // The inputs are replaced by their next iterate
    CUDA_conjugate_gradient_4f(size, pX, pR, pP, pAOut);

    this->m_A->Modified();
    P_k->Modified();
    }

  P_k = NULL;
  R_k = NULL;
  AOut = NULL;
}
