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

#ifndef HECTOR_POSE_ESTIMATION_MEASUREMENT_UPDATE_H
#define HECTOR_POSE_ESTIMATION_MEASUREMENT_UPDATE_H

#include <boost/type_traits/is_base_of.hpp>

namespace hector_pose_estimation {

class MeasurementUpdate
{
public:
  MeasurementUpdate() {}
  virtual ~MeasurementUpdate() {}

  virtual bool hasCovariance() const { return false; }
};

template <class MeasurementModel>
class Update_ : public MeasurementUpdate {
public:
  typedef Update_<MeasurementModel> Type;
  typedef typename MeasurementModel::MeasurementVector Vector;
  typedef typename MeasurementModel::NoiseCovariance Covariance;

  Update_()
    : y_(MeasurementModel::MeasurementDimension)
    , has_covariance_(false)
  {}
  Update_(Vector const& y)
    : y_(MeasurementModel::MeasurementDimension)
    , has_covariance_(false)
  {
    setValue(y);
  }
  Update_(double y)
    : y_(MeasurementModel::MeasurementDimension)
    , has_covariance_(false)
  {
    setValue(y);
  }
  virtual ~Update_() {}

  virtual void setValue(Vector const& y) { y_ = y; }
  virtual void setValue(double y) { y_(1) = y; }

  virtual void setCovariance(Covariance const& R) { R_ = R; has_covariance_ = true; }
  virtual Vector const &getVector() const { return y_; }
  virtual Covariance const &getCovariance() const { return R_; }

  virtual Vector &operator=(Vector const& y) { setValue(y); return y_; }
  virtual Vector &operator=(double y) { setValue(y); return y_; }

  virtual bool hasCovariance() const { return has_covariance_; }

protected:
  Vector y_;
  Covariance R_;
  bool has_covariance_;
};

namespace internal {
  template <class ConcreteModel, class ConcreteUpdate = Update_<ConcreteModel>, class Enable = void>
  struct UpdateInspector {
    static typename ConcreteModel::MeasurementVector const& getVector(const ConcreteUpdate &update, const ConcreteModel*) { return *static_cast<typename ConcreteModel::MeasurementVector *>(0); }
    static typename ConcreteModel::NoiseCovariance const& getCovariance(const ConcreteUpdate &update, const ConcreteModel*) { return *static_cast<typename ConcreteModel::NoiseCovariance *>(0); }
  };

  template <class ConcreteModel, class ConcreteUpdate>
  struct UpdateInspector<ConcreteModel, ConcreteUpdate, typename boost::enable_if<boost::is_base_of<Update_<ConcreteModel>,ConcreteUpdate> >::type >
  {
    static typename ConcreteModel::MeasurementVector const& getVector(const ConcreteUpdate &update, const ConcreteModel*) { return update.getVector(); }
    static typename ConcreteModel::NoiseCovariance const& getCovariance(const ConcreteUpdate &update, const ConcreteModel*) { return update.getCovariance(); }
  };
}

} // namespace hector_pose_estimation

#endif // HECTOR_POSE_ESTIMATION_MEASUREMENT_UPDATE_H
