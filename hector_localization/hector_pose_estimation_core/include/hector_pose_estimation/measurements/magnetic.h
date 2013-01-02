//=================================================================================================
// Copyright (c) 2011, Johannes Meyer, TU Darmstadt
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Flight Systems and Automatic Control group,
//       TU Darmstadt, nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=================================================================================================

#ifndef HECTOR_POSE_ESTIMATION_MAGNETIC_H
#define HECTOR_POSE_ESTIMATION_MAGNETIC_H

#include <hector_pose_estimation/measurement.h>
#include <hector_pose_estimation/global_reference.h>
#include <bfl/wrappers/matrix/matrix_wrapper.h>

namespace hector_pose_estimation {

class MagneticModel : public MeasurementModel {
public:
  static const unsigned int MeasurementDimension = 3;
  typedef ColumnVector_<MeasurementDimension> MeasurementVector;
  typedef SymmetricMatrix_<MeasurementDimension> NoiseCovariance;

  MagneticModel();
  virtual ~MagneticModel();

  virtual bool init();
  virtual SystemStatus getStatusFlags() const;

  virtual ColumnVector ExpectedValueGet() const;
  virtual Matrix dfGet(unsigned int i) const;

  double getMagneticHeading(const MeasurementVector& y) const;
  double getTrueHeading(const MeasurementVector& y) const;

  void setReference(const GlobalReference::Heading &reference_heading);
  bool hasMagnitude() const { return magnitude_ != 0.0; }

protected:
  double stddev_;
  double declination_, inclination_, magnitude_;
  void updateMagneticField();

  MeasurementVector magnetic_field_north_;
  MeasurementVector magnetic_field_reference_;
  mutable Matrix C_full_;
};

class Magnetic : public Measurement_<MagneticModel>
{
public:
  Magnetic(const std::string& name = "height");
  virtual ~Magnetic() {}

  virtual void onReset();

  virtual MeasurementVector const& getVector(const Update &update);
  virtual NoiseCovariance const& getCovariance(const Update &update);

  virtual bool beforeUpdate(PoseEstimation &estimator, const Update &update);

private:
  bool auto_heading_;
  GlobalReference *reference_;

  MeasurementVector y_;
  NoiseCovariance R_;
};

} // namespace hector_pose_estimation

#endif // HECTOR_POSE_ESTIMATION_MAGNETIC_H
