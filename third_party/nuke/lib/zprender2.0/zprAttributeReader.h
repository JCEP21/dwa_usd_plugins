//
// Copyright 2020 DreamWorks Animation
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

/// @file zprAttributeReader.h
///
/// @author Jonathan Egstad


#ifndef zprAttributeReader_h
#define zprAttributeReader_h

#include "RayShader.h"


namespace zpr {


/*!
*/
class ZPR_EXPORT zprAttributeReader : public RayShader
{
  protected:


  public:
    static const ShaderDescription description;
    /*virtual*/ const char* zprShaderClass() const { return description.shaderClass(); }
    static const InputKnobList  input_defs;
    static const OutputKnobList output_defs;
    /*virtual*/ const InputKnobList&  getInputKnobDefinitions() const  { return input_defs;  }
    /*virtual*/ const OutputKnobList& getOutputKnobDefinitions() const { return output_defs; }

    //!
    zprAttributeReader();
    ~zprAttributeReader();

    //!
    void setFilename(const char* path,
                     int         version);


    /*virtual*/ void validateShader(bool                            for_real,
                                    const RenderContext*            rtx,
                                    const DD::Image::OutputContext* op_ctx=NULL);
    /*virtual*/ void evaluateSurface(RayShaderContext& stx,
                                     Fsr::Pixel&       out);

};


} // namespace zpr

#endif

// end of zprAttributeReader.h

//
// Copyright 2020 DreamWorks Animation
//
