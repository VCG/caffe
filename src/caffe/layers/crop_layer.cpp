#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>


#include "caffe/layer.hpp"
#include "caffe/layers/crop_layer.hpp"
#include "caffe/net.hpp"


namespace caffe {

template <typename Dtype>
void CropLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  // All logic that depends only on the number of dimensions is here,
  // the rest is in Reshape because it depends on Blob size.
  // bottom[0] supplies the data
  // bottom[1] supplies the size
  const CropParameter& param = this->layer_param_.crop_param();
  CHECK_EQ(bottom.size(), 2) << "Wrong number of bottom blobs.";
  int_tp input_dim = bottom[0]->num_axes();
  const int_tp start_axis = bottom[0]->CanonicalAxisIndex(param.axis());
  CHECK_LT(start_axis, input_dim) << "crop axis bigger than input dim";
  if (param.offset_size() > 1) {
    // the number of crop values specified must be equal to the number
    // of dimensions following axis
    CHECK_EQ(start_axis + param.offset_size(), input_dim)
      << "number of offset values specified must be equal to the number of "
      << "dimensions following axis.";
  }
}

template <typename Dtype>
void CropLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  const CropParameter& param = this->layer_param_.crop_param();
  int_tp input_dim = bottom[0]->num_axes();
  const int_tp start_axis = bottom[0]->CanonicalAxisIndex(param.axis());

  // initialize all offsets to 0
  offsets = vector<int_tp>(input_dim, 0);
  // initialize new shape to bottom[0]
  vector<int_tp> new_shape(bottom[0]->shape());

  // apply crops
  for (int_tp i = 0; i < input_dim; ++i) {
    int_tp crop_offset = 0;
    int_tp new_size    = bottom[0]->shape(i);
    if (i >= start_axis) {
      new_size = bottom[1]->shape(i);

      if (param.offset_size() == 1) {
        // if only one crop value is supplied, crop all dimensions after axis
        // by this crop value
        crop_offset = param.offset(0);
      } else if (param.offset_size() > 1) {
        // crop values specified must be equal to the number of dimensions
        // following axis
        crop_offset = param.offset(i - start_axis);
      }
    }
    // Check that the image we are cropping minus the margin is bigger
    // than the destination image.
    CHECK_GE(bottom[0]->shape(i) - crop_offset,
             bottom[1]->shape(i))
        << "invalid crop parameters in dimension: " << i;
    // Now set new size and offsets
    new_shape[i] = new_size;
    offsets[i] = crop_offset;
  }
  top[0]->Reshape(new_shape);
}

// recursive copy function
template <typename Dtype>
void CropLayer<Dtype>::crop_copy(const vector<Blob<Dtype>*>& bottom,
             const vector<Blob<Dtype>*>& top,
             const vector<int_tp>& offsets,
             vector<int_tp> indices,
             int_tp cur_dim,
             const Dtype* src_data,
             Dtype* dest_data,
             bool is_forward) {
  if (cur_dim + 1 < top[0]->num_axes()) {
    // We are not yet at the final dimension, call copy recursively
    for (int_tp i = 0; i < top[0]->shape(cur_dim); ++i) {
      indices[cur_dim] = i;
      crop_copy(bottom, top, offsets, indices, cur_dim+1,
                src_data, dest_data, is_forward);
    }
  } else {
    // We are at the last dimensions, which is stored continously in memory
    for (int_tp i = 0; i < top[0]->shape(cur_dim); ++i) {
      // prepare index vector reduced(red) and with offsets(off)
      std::vector<int_tp> ind_red(cur_dim, 0);
      std::vector<int_tp> ind_off(cur_dim+1, 0);
      for (int_tp j = 0; j < cur_dim; ++j) {
          ind_red[j] = indices[j];
          ind_off[j] = indices[j] + offsets[j];
      }
      ind_off[cur_dim] = offsets[cur_dim];
      // do the copy
      if (is_forward) {
        caffe_copy(top[0]->shape(cur_dim),
            src_data + bottom[0]->offset(ind_off),
            dest_data + top[0]->offset(ind_red));
      } else {
        // in the backwards pass the src_data is top_diff
        // and the dest_data is bottom_diff
        caffe_copy(top[0]->shape(cur_dim),
            src_data + top[0]->offset(ind_red),
            dest_data + bottom[0]->offset(ind_off));
      }
    }
  }
}

template <typename Dtype>
void CropLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  std::vector<int_tp> indices(top[0]->num_axes(), 0);
  const Dtype* bottom_data = bottom[0]->cpu_data();
  Dtype* top_data = top[0]->mutable_cpu_data();
  crop_copy(bottom, top, offsets, indices, 0, bottom_data, top_data, true);
}

template <typename Dtype>
void CropLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const Dtype* top_diff = top[0]->cpu_diff();
  Dtype* bottom_diff = bottom[0]->mutable_cpu_diff();

  if (propagate_down[0]) {
    caffe_set(bottom[0]->count(), static_cast<Dtype>(0), bottom_diff);
    std::vector<int_tp> indices(top[0]->num_axes(), 0);
    crop_copy(bottom, top, offsets, indices, 0, top_diff, bottom_diff, false);
  }
}

#ifdef CPU_ONLY
STUB_GPU(CropLayer);
#endif

INSTANTIATE_CLASS(CropLayer);
REGISTER_LAYER_CLASS(Crop);

}  // namespace caffe
