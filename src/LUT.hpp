/*
 *  This file is part of GimpLensfun.
 *
 *  The sourcecode for the lookup table has been taken from Rawtherapee.
 *  Copyright (c) 2011 Jan Rinze Peterzon (janrinze@gmail.com)
 *
 *  GimpLensfun is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GimpLensfun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GimpLensfun.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 *  Declaration of flexible Lookup Tables
 *
 *  Usage:
 *
 *      LUT<type> name (size);
 *              LUT<type> name (size, flags);
 *
 *              creates an array which is valid within the normal C/C++ scope "{ ... }"
 *
 *      access to elements is a simple as:
 *
 *              LUT<float> my_lut (10);
 *              float value = my_lut[3];
 *              float value = my_lut[2.5]; // this will interpolate
 *
 *      when using a float type index it will interpolate the lookup values
 *
 *      extra setting in flags: (clipping is set by default)
 *      LUT_CLIP_ABOVE
 *      LUT_CLIP_BELOW
 *
 *      example:
 *              LUT<float> my_lut (10,LUT_CLIP_BELOW);
 *              float value = my_lut[22.5];  // this will extrapolate
 *              float value = my_lut[-22.5]; // this will not extrapolate
 *
 *              LUT<float> my_lut (10,0); // this will extrapolate on either side
 *
 *      shotcuts:
 *
 *              LUTf stands for LUT<float>
 *              LUTi stands for LUT<int>
 *              LUTu stands for LUT<unsigned int>
 */

#ifndef LUT_H_
#define LUT_H_

#include <glib/gtypes.h>

// bit representations of flags
#define LUT_CLIP_BELOW 1
#define LUT_CLIP_ABOVE 2

#define LUTf LUT<gfloat>
#define LUTi LUT<gint>
#define LUTu LUT<guint>

#include <cstring>

template<typename T>
class LUT
{
 private:
  // List of variables ordered to improve cache speed
  guint maxs;
  T *data;
  guint clip, size, owner;
 public:
  LUT (guint s, guint flags = 0xfffffff)
  {
    clip  = flags;
    data  = new T[s];
    owner = 1;
    size  = s;
    maxs  = size - 2;
  }
  void
  operator() (guint s, guint flags = 0xfffffff)
  {
    if (owner && data)
      delete[] data;
    clip  = flags;
    data  = new T[s];
    owner = 1;
    size  = s;
    maxs  = size - 2;
  }

  LUT (guint s, T *source)
  {
    data  = new T[s];
    owner = 1;
    size  = s;
    maxs  = size - 2;
    for (gint i = 0; i < s; i++)
    {
      data[i] = source[i];
    }
  }

  LUT ()
  {
    data  = nullptr;
    owner = 1;
    size  = 0;
    maxs  = 0;
  }

  ~LUT ()
  {
    if (owner)
      delete[] data;
  }

  LUT<T> &
  operator= (const LUT<T> &rhs)
  {
    if (this != &rhs)
    {
      if (rhs.size > this->size)
      {
        delete[] this->data;
        this->data = nullptr;
      }
      if (this->data == nullptr) this->data = new T[rhs.size];
      this->clip  = rhs.clip;
      this->owner = 1;
      memcpy (this->data, rhs.data, rhs.size * sizeof (T));
      this->size = rhs.size;
      this->maxs = this->size - 2;
    }

    return *this;
  }
  // Use with integer indices
  T &
  operator[] (gint index)
  {
    if (((guint) index) < size) return data[index];
    else
    {
      if (index < 0)
        return data[0];
      else
        return data[size - 1];
    }

  }
  // Use with float indices
  T
  operator[] (gfloat index)
  {
    auto idx = (gint) index;  // Don't use floor! The difference in negative space is no problems here
    if (((guint) idx) > maxs)
    {
      if (idx < 0)
      {
        if (clip & LUT_CLIP_BELOW)
          return data[0];
        idx = 0;
      }
      else
      {
        if (clip & LUT_CLIP_ABOVE)
          return data[size - 1];
        idx = maxs;
      }
    }
    gfloat diff = index - (gfloat) idx;
    T      p1   = data[idx];
    T      p2   = data[idx + 1] - p1;
    return (p1 + p2 * diff);
  }

  explicit operator bool ()
  {
    return size > 0;
  }

  void
  clear ()
  {
    memset (data, 0, size * sizeof (T));
  }
};

#endif /* LUT_H_ */
