//
// Copyright 2019 DreamWorks Animation
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

/// @file Fuser/FuserSceneLoader.cpp
///
/// @author Jonathan Egstad

#include "SceneLoader.h"
#include "NukeKnobInterface.h"
#include "ExecuteTargetContexts.h"

#include <DDImage/Knobs.h>
#include <DDImage/Enumeration_KnobI.h>
#include <DDImage/SceneView_KnobI.h>


namespace Fsr {


#ifdef FUSER_USE_KNOB_RTTI
const char* SceneLoaderRTTIKnob = "FsrSceneLoader";
#endif


#define SCENEGRAPH_MAX_DEPTH_DEFAULT 6


//------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------


/*! Custom knob type to help scene loading occur early enough
    to reliably affect transform knob values.
*/
class LoadHelperKnob : public DD::Image::Knob
{
    SceneLoader* parent;
    bool         do_first_load;


  public:
    /*virtual*/ const char* Class() const { return "Loader"; }

    LoadHelperKnob(DD::Image::Knob_Closure* kc,
                   SceneLoader*             _parent,
                   const char*              name) :
        DD::Image::Knob(kc, name),
        parent(_parent),
        do_first_load(true)
    {
        //std::cout << "LoadHelperKnob::ctor(" << this << ")" << std::endl;
    }


    /*! This method causes the scene node to be loaded upon initial script
        load, and any time afterwards when knob values need to be overrriden
        before Op::validate() is used.
    */
    /*virtual*/ void store(DD::Image::StoreType            type,
                           void*                           data,
                           DD::Image::Hash&                hash,
                           const DD::Image::OutputContext& context)
    {
        //std::cout << "LoadHelperKnob::store()" << std::endl;

        assert(parent);

        //-------------------------------------------------------------------------
        // Check if 'read_from_file' is true, 'scene_file' string is not empty but
        // 'scene_node' IS empty.
        // This will update 'scene_node' with a default node path.
        //-------------------------------------------------------------------------
        DD::Image::Knob* k = op()->knob("scene_loaded_legacy");
        if (k && (k->get_value() > 0.5))
        {
            parent->checkForValidNodePath();
            k->set_value(0.0);
        }

        parent->updateSceneNode(hash,
                                NULL/*&context*/,
                                do_first_load/*force_update*/,
                                false/*force_load*/);

        do_first_load = false;
    }


    ///*virtual*/ void        append(DD::Image::Hash&, const DD::Image::OutputContext*);
    ///*virtual*/ void        changed();
    ///*virtual*/ bool        from_script(const char*);
    ///*virtual*/ const char* get_text(const OutputContext*) const;
    ///*virtual*/ void        to_script(std::ostream&, const OutputContext*, bool quote) const;
    ///*virtual*/ bool        not_default() const;
};


/*! Check if 'read_from_file' is true, 'scene_file' string is not empty but
    'scene_node' IS empty.

    This is a special case as it likely indicates that the script was saved by
    the non-Fuser nodes which don't appear to explicitly write a selected node
    if the node was assigned by default. ie the user selected a scene file and
    the loader code automatically picked the first valid node, but didn't save
    that node path into the script on save.

    To fix this we replicate the default node search but store the resulting
    node path into the knob so that is gets saved and we don't have to do this
    again on the next load...
*/
void
SceneLoader::checkForValidNodePath()
{
    DD::Image::Op* op = sceneOp();
    assert(op);

    DD::Image::Knob* kRead = op->knob("read_from_file");
    if (!kRead || (kRead->get_value() < 0.5))
        return; // no read knob or it's turned off

    DD::Image::Knob* kFile = op->knob("scene_file");
    DD::Image::Knob* kNode = op->knob("scene_node");
    if (!kFile || !kNode)
        return; // just in case...

    const char* scene_file((!kFile->get_text()) ? "" : kFile->get_text());
    const char* scene_node((!kNode->get_text()) ? "" : kNode->get_text());

    //std::cout << op->node_name() << " read_from_file='" << kRead->get_value() << "'";
    //std::cout << ", scene_file='" << scene_file << "'" << ", scene_node='" << scene_node << "'";
    //std::cout << std::endl;

    if (!scene_file || !scene_file[0])
        return; // no scene file path, don't do anything

    if (scene_node && scene_node[0])
        return; // have both paths, nothing to do

    //std::cout << op->node_name() << ":checkForValidNodePath() read_from_file='" << kRead->get_value() << "'";
    //std::cout << ", scene_file='" << scene_file << "'" << ", scene_node='" << scene_node << "'";
    //std::cout << std::endl;

    // Missing node path, let's try to find one:
    const std::string node_path = findDefaultNode(scene_file, false/*debug*/);
    if (!node_path.empty())
        kNode->set_text(node_path.c_str());

    //std::cout << "    found node='" << node_path << "'" << std::endl;
}


//------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------


/*!
*/
SceneLoader::SceneLoader(bool read_enabled) :
    NodeIOInterface(),
    SceneOpExtender(),
    k_scene_loaded_legacy(false),
    k_editable(true),
    m_loader_error(false)
{
    k_scene_ctls.read_enabled        = read_enabled;
    k_scene_ctls.file                = "";
    k_scene_ctls.file_version        = 0;
    //
    k_scene_ctls.node_path           = "";
    //
    k_scene_ctls.lock_read_frame     = false;
    k_scene_ctls.frame_offset        = 0.0;
    k_scene_ctls.frame_origin        = 0.0;
    k_scene_ctls.read_frame          = 0.0;
    k_scene_ctls.frames_per_second   = 24.0; 
    k_scene_ctls.lock_read_view      = false;
    k_scene_ctls.read_view           = 0; // main
    //
    k_scene_ctls.decompose_xform_order = Fsr::SRT_ORDER;
    k_scene_ctls.decompose_rot_order   = Fsr::ZXY_ORDER;
    k_scene_ctls.T_enable            = true;
    k_scene_ctls.R_enable            = true;
    k_scene_ctls.S_enable            = true;
    k_scene_ctls.euler_filter_enable = true;
    k_scene_ctls.parent_extract_enable = true;
    k_scene_ctls.read_debug          = false;
    k_scene_ctls.archive_debug       = false;
    //
    //m_scene_read_error               = SceneLoader::NO_ERROR;

    kSceneView = NULL;
}


/*! Returns true if Op is a Fuser SceneLoader.

    For a statically-linked Fuser lib this is a hack - we test for a
    dummy knob so we can test the class without using RTTI which
    fails when dso plugins are statically linked to this class.
*/
/*static*/
bool
SceneLoader::isOpSceneLoader(DD::Image::Op* op)
{
#ifdef FUSER_USE_KNOB_RTTI
    // HACK!!!!: Test for dummy knob so we can test for class without using RTTI...:
    return (op && op->knob(SceneLoaderRTTIKnob) != NULL);
#else
    // TODO: this probably does not work due to multiple-inheritance...:
    return (dynamic_cast<SceneLoader*>(op) != NULL);
#endif
}


//------------------------------------------------------------------------------------


//
// Scene graph browser push-button script.
// Open the scenegraph browser and process the resulting selection.
// Access an external Python file for the functions rather than hardcoding it.
//
const char* const py_scenegraph_browser =
    "import nuke\n"
    "try:\n"
    "    import sceneloader_support\n"
    "    _this = nuke.thisNode()\n"
    "    sceneFileKnob = _this['scene_file']\n"
    "    if sceneFileKnob is not None:\n"
    "        sceneNodePath = sceneloader_support.sceneBrowser(node=_this, path=sceneFileKnob.getText())\n"
    "        sceneNodeKnob = _this['scene_node']\n"
    "        if sceneNodePath != '' and sceneNodeKnob is not None:\n"
    "            sceneNodeKnob.setText(sceneNodePath)\n"
    ""
    "except (ImportError), e:\n"
    "    print 'Unable to import sceneloader_support python module.'\n"
    "";


/*!
*/
/*virtual*/
void
SceneLoader::addSceneLoaderKnobs(DD::Image::Knob_Callback f,
                                 bool                     group_open,
                                 bool                     show_xform_knobs,
                                 bool                     show_hierarchy)
{
    //std::cout << "  SceneLoader::addSceneLoaderKnobs() makeKnobs=" << f.makeKnobs() << std::endl;

#ifdef FUSER_USE_KNOB_RTTI
    // HACK!!!!: Dummy knob so we can test for class without using RTTI...:
    int dflt=0; DD::Image::Int_knob(f, &dflt, SceneLoaderRTTIKnob, DD::Image::INVISIBLE);
        DD::Image::SetFlags(f, DD::Image::Knob::DO_NOT_WRITE | DD::Image::Knob::NO_ANIMATION | DD::Image::Knob::NO_RERENDER);
#endif

    //----------------------------------------

    //DD::Image::Divider(f, "@b;Scene File Import");
    DD::Image::BeginGroup(f, "scene_file_import", "@b;Scene File Import");
    {
        DD::Image::SetFlags(f, DD::Image::Knob::DO_NOT_WRITE);
        if (group_open)
            DD::Image::ClearFlags(f, DD::Image::Knob::CLOSED);
        else
            DD::Image::SetFlags(f, DD::Image::Knob::CLOSED);

        {
            DD::Image::Bool_knob(f, &k_scene_ctls.read_enabled, "read_from_file", "read from file");
                DD::Image::SetFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE);

            //----------------------------------------

#if 0
            DD::Image::Button(f, "lock_button", "Lock");
                DD::Image::SetFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::Tooltip(f, "Enable read from file mode, locking the synced knobs.");
            DD::Image::Button(f, "unlock_button", "Unlock");
                DD::Image::Tooltip(f, "Disable read from file mode, unlocking the synced knobs.");
#endif
            DD::Image::Spacer(f, 10);
            DD::Image::Script_knob(f, "knob scene_file_version [expr [value scene_file_version]+1]", "Reload");
                DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::SetFlags(f, DD::Image::Knob::DO_NOT_WRITE |
                                       DD::Image::Knob::NO_UNDO |
                                       DD::Image::Knob::NO_ANIMATION);
                DD::Image::Tooltip(f, "Re-read the node data from the scene file only if the 'read enable' "
                                        "switch is enabled.");
            //DD::Image::Button(f, "force_load_button", "Force Load");
            //    DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
            //    DD::Image::Tooltip(f, "Ignores the 'read enable' switch and forces immediate reading of node data from "
            //                            "the scene file, destroying any existing data.");
            DD::Image::Bool_knob(f, &k_scene_ctls.read_debug, "scene_read_debug", "debug node read");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::Tooltip(f, "Print debug info during file loading.");
            DD::Image::Bool_knob(f, &k_scene_ctls.archive_debug, "scene_archive_debug", "debug scene read");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::SetFlags(f, DD::Image::Knob::ENDLINE);
                DD::Image::Tooltip(f, "Print debug info during archive file handling.");
            //DD::Image::Newline(f);

            //----------------------------------------

            DD::Image::File_knob(f, &k_scene_ctls.file, "scene_file", "scene file", DD::Image::Geo_File);
                DD::Image::SetFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE |
                                       DD::Image::Knob::NO_MULTIVIEW);
            DD::Image::Int_knob(f, &k_scene_ctls.file_version, "scene_file_version", DD::Image::INVISIBLE);

            //----------------------------------------

            DD::Image::String_knob(f, &k_scene_ctls.node_path, "scene_node", "scene node");
                DD::Image::SetFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE |
                                       DD::Image::Knob::NO_MULTIVIEW);
            DD::Image::PyScript_knob(f, py_scenegraph_browser, "scenegraph_browser", "@File_Knob");
                DD::Image::SetFlags(f, DD::Image::Knob::DO_NOT_WRITE |
                                       DD::Image::Knob::NO_UNDO |
                                       DD::Image::Knob::NO_ANIMATION);
                DD::Image::SetFlags(f, DD::Image::Knob::ENDLINE);
                DD::Image::Tooltip(f, "Open a custom scene file browser to view and optionally pick an "
                                      "object path to place in 'scene_node'.\n"
                                      "\n"
                                      "When pushed this button attempts to import a Python module named "
                                      "'sceneloader_support' and call its 'sceneBrowser()' method.\n"
                                      "See the example 'sceneloader_support.py' file for additional "
                                      "implementation and argument details.");
            //DD::Image::Newline(f);

            // These stop legacy scripts from throwing errors by attempting to translate
            // most of the old values to new values. I think some of the values are moot
            // for Alembic and USD as we're not supporting FBX:
            DD::Image::Bool_knob(f, &k_scene_loaded_legacy, "scene_loaded_legacy", DD::Image::INVISIBLE);
            DD::Image::Obsolete_knob(f, "file",           "knob scene_file $value; knob scene_loaded_legacy true"        );
            DD::Image::Obsolete_knob(f, "version",        "knob scene_file_version $value; knob scene_loaded_legacy true");
            DD::Image::Obsolete_knob(f, "fbx_node_name",  "knob scene_node [lindex $value [expr [lindex $value 0]+1]]; knob scene_loaded_legacy true");
            DD::Image::Obsolete_knob(f, "fbx_take_name",  "knob scene_loaded_legacy true");
            // TODO: move these to the FuserCameraOp class? They really aren't camera-only options...
            //DD::Image::Obsolete_knob(f, "frame_rate",     "knob scene_loaded_legacy true"); // CameraOps only
            DD::Image::Obsolete_knob(f, "use_frame_rate", "knob scene_loaded_legacy true"); // CameraOps only

        }

        //----------------------------------------

        if (show_hierarchy)
        {
            DD::Image::BeginGroup(f, "scene_file_hierarchy", "scene file contents");
            {
                DD::Image::SetFlags(f, DD::Image::Knob::CLOSED |
                                       DD::Image::Knob::DO_NOT_WRITE);

                int default_max_depth = SCENEGRAPH_MAX_DEPTH_DEFAULT;
                DD::Image::Int_knob(f, &default_max_depth, "scenegraph_max_depth", "max depth");
    	            DD::Image::SetFlags(f, DD::Image::Knob::DO_NOT_WRITE |
                                           DD::Image::Knob::NO_MULTIVIEW |
                                           DD::Image::Knob::NO_RERENDER);
                    DD::Image::Tooltip(f, "Restricts the maximum node hierarchy depth to improve scene "
                                          "loading speed.\n"
                                          "\n"
                                          "Nuke's scenegraph viewer does not allow progressive expansion "
                                          "as a user gradually opens the hierarchy, so to speed up the "
                                          "loading of large scenes this control helps limit the number "
                                          "of nodes being loaded and potentially not displayed.");

                int dummy_int = 0;
                const char* empty_list = { 0 };
                kSceneView = DD::Image::SceneView_knob(f, &dummy_int, &empty_list, "scene_file_nodes", "");
                    DD::Image::Tooltip(f, "List of available nodes in scene file");
    	            DD::Image::SetFlags(f, DD::Image::Knob::STARTLINE |
                                           DD::Image::Knob::DO_NOT_WRITE |
                                           DD::Image::Knob::NO_ANIMATION |
                                           DD::Image::Knob::KNOB_CHANGED_ALWAYS |
                                           DD::Image::Knob::SINGLE_SELECTION_ONLY);
            }
            DD::Image::EndGroup(f);
        }

        //----------------------------------------

        if (1/*show_time_knobs*/)
        {
            DD::Image::BeginGroup(f, "scene_file_frame_ctrls", "frame / view options");
            {
                DD::Image::SetFlags(f, DD::Image::Knob::CLOSED |
                                       DD::Image::Knob::DO_NOT_WRITE);

                DD::Image::Bool_knob(f, &k_scene_ctls.lock_read_frame, "lock_read_frame", "lock read frame");
                    DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE);
                    DD::Image::Tooltip(f, "If enabled lock the reader to use the manually-set frame number.");
                DD::Image::Double_knob(f, &k_scene_ctls.read_frame, "read_frame", "");
                    DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE |
                                           DD::Image::Knob::DISABLED |
                                           DD::Image::Knob::NO_MULTIVIEW);
                    DD::Image::ClearFlags(f, DD::Image::Knob::SLIDER |
                                             DD::Image::Knob::STARTLINE);
                    DD::Image::Tooltip(f, "Use this frame number when 'lock read frame' is enabled.\n"
                                          "This control can be animated to read any arbitrary frame speed curve.");
                DD::Image::Newline(f);

                DD::Image::Double_knob(f, &k_scene_ctls.frame_origin, "frame_origin", "frame: origin");
                    DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE |
                                           DD::Image::Knob::NO_MULTIVIEW |
                                           DD::Image::Knob::NO_ANIMATION);
                    DD::Image::ClearFlags(f, DD::Image::Knob::SLIDER);
                    DD::Image::Tooltip(f, "Origin of the incoming frame range. Any frame rate change is scaled from this "
                                          "point allowing it to remain unchanged on the output timeline.\n"
                                          "This can be very useful to keep an important frame in a time-warped range from "
                                          "shifting unexpectedly on the timeline. For example if shot animation begins at "
                                          "frame 101 but there's some preroll keys at frames 50-100 then setting this "
                                          "control to 101 will keep that frame from changing when a frame rate change is "
                                          "applied, preserving the start of the shot.\n");
                DD::Image::Double_knob(f, &k_scene_ctls.frames_per_second, DD::Image::IRange(1,96), "frame_rate", "rate");
                    DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE |
                                           DD::Image::Knob::NO_MULTIVIEW |
                                           DD::Image::Knob::NO_ANIMATION);
                    DD::Image::ClearFlags(f, DD::Image::Knob::SLIDER |
                                             DD::Image::Knob::STARTLINE);
                    DD::Image::Tooltip(f, "This is the frame rate (frames per second) used to sample the geometry file.\n"
                                          "If this rate is lower than the rate encoded in the file the effect is to "
                                          "slow down the animation. For example if the file was animated at 24 fps and "
                                          "frame_rate is set to 12, the animation will read at half speed.");
                DD::Image::Double_knob(f, &k_scene_ctls.frame_offset, "frame_offset", "output offset");
                    DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE |
                                           DD::Image::Knob::NO_MULTIVIEW |
                                           DD::Image::Knob::NO_ANIMATION);
                    DD::Image::ClearFlags(f, DD::Image::Knob::SLIDER |
                                             DD::Image::Knob::STARTLINE);
                    DD::Image::Tooltip(f, "Offset the incoming keyframes in time, applied after any frame rate change.\n"
                                          "This will shift the final, possibly time-warped keyframes in the timeline.");
                DD::Image::Newline(f);

                DD::Image::Bool_knob(f, &k_scene_ctls.lock_read_view, "lock_read_view", "lock read view");
                    DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE);
                    DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                    DD::Image::Tooltip(f, "If enabled lock the reader to use the manually-set view.");
                DD::Image::OneView_knob(f, &k_scene_ctls.read_view, "read_view", "");
                    DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE |
                                           DD::Image::Knob::DISABLED);
                    DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                    DD::Image::Tooltip(f, "Use this view when 'lock view' is enabled.");

            }
            DD::Image::Divider(f);
            DD::Image::EndGroup(f);
        }

        //----------------------------------------

        if (show_xform_knobs)
        {
            DD::Image::Enumeration_knob(f, &k_scene_ctls.decompose_xform_order, xform_orders, "decompose_xform_order", "xform decompose");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::Tooltip(f, "When decomposing an imported matrix transform this is the "
                                      "preferred order of the output transformation operations.");
            DD::Image::Enumeration_knob(f, &k_scene_ctls.decompose_rot_order, rotation_orders, "decompose_rot_order", "");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::Tooltip(f, "When decomposing an imported matrix transform this is the "
                                      "preferred order of the output rotations.");
            DD::Image::Bool_knob(f, &k_scene_ctls.euler_filter_enable, "euler_filter_enable", "euler filter");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::Tooltip(f, "Check for possible euler flips in the rotation data.");
            DD::Image::Bool_knob(f, &k_scene_ctls.parent_extract_enable, "parent_extract_enable", "separate parent xform");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::Tooltip(f, "When loading xform node data from a hierarchical scene file the node's parent transform "
                                      "is placed here if this is enabled.\n"
                                      "If not enabled the parent transform is combined with the node's local transform.\n");
                DD::Image::SetFlags(f, DD::Image::Knob::ENDLINE);
            //
            DD::Image::Bool_knob(f, &k_scene_ctls.T_enable, "translate_enable", "get translate");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::Tooltip(f, "Enable importing translation curves.");
            DD::Image::Bool_knob(f, &k_scene_ctls.R_enable, "rotate_enable", "get rotate");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::Tooltip(f, "Enable importing rotation curves.");
            DD::Image::Bool_knob(f, &k_scene_ctls.S_enable, "scale_enable", "get scale");
                DD::Image::SetFlags( f, DD::Image::Knob::EARLY_STORE);
                DD::Image::ClearFlags(f, DD::Image::Knob::STARTLINE);
                DD::Image::Tooltip(f, "Enable importing scale curves.");
                DD::Image::SetFlags(f, DD::Image::Knob::ENDLINE);
            //DD::Image::Newline(f);
        }

#if 0
        Newline(f, "use hero camera");
        Bool_knob(f, &k_copy_specular, "copy_hero_camera", "");
            SetFlags(f, Knob::EARLY_STORE);
            Tooltip(f, "Always use the camera matrix from the hero view camera.");
        OneView_knob(f, &k_hero_view, "hero_view", "");
            SetFlags(f, Knob::EARLY_STORE);
            Tooltip(f, "Normally is the left (LFT) view.");
        //
        // Need early store enabled to make frame offset knob values availalbe for split_input():
        Double_knob(f, &k_cam_frame_offset, IRange(-1.0, 1.0), "camera_frame_offset", "camera frame offset");
            SetFlags(f, Knob::STARTLINE | Knob::EARLY_STORE);
            ClearFlags(f, Knob::LOG_SLIDER);
            Tooltip(f, "Apply this time offset to the camera input.");
        Enumeration_knob(f, &k_cam_frame_mode, frame_offset_mode, "camera_offset_mode", "");
            SetFlags(f, Knob::STARTLINE | Knob::EARLY_STORE);
            ClearFlags(f, Knob::STARTLINE);
#endif

    }
    DD::Image::EndGroup(f);
    //----------------------------------------

    //------------------------------------------------------------------------------------
    // This custom knob's store() method calls updateSceneNode() to make the
    // first scene load to happen after script load. See below.
    // Keep this knob's declaration after all the other knobs that the loader needs to hash up.
    CustomKnob1(LoadHelperKnob, f, this, "scene_load_evaluator");
        DD::Image::SetFlags(f, DD::Image::Knob::EARLY_STORE | DD::Image::Knob::KNOB_CHANGED_ALWAYS);
    //------------------------------------------------------------------------------------
}


/*! Adds additional OpenGL display option controls.
    Currently it just adds the 'editable' switch.
*/
/*virtual*/
void
SceneLoader::addDisplayOptionsKnobs(DD::Image::Knob_Callback f)
{
    DD::Image::Bool_knob(f, &k_editable, "editable", "editable");
        DD::Image::Tooltip(f, "Turn off to prevent changing values when manipulating in Viewer.");
}


//------------------------------------------------------------------------------------


/*! Is scene loader enabled?
    Base class returns the state of the 'read_from_file' knob.
*/
/*virtual*/
bool
SceneLoader::isSceneLoaderEnabled()
{
    if (!sceneOp())
        return false;
    return getBoolValue(sceneOp()->knob("read_from_file"), false);
}


/*!
*/
/*virtual*/
int
SceneLoader::knobChanged(DD::Image::Knob* k,
                         int              call_again)
{
    //std::cout << "SceneLoader::knobChanged('" << k->name() << "')" << std::endl;
    DD::Image::Op* op = sceneOp();
    assert(op);

    const bool scene_loader_enabled = isSceneLoaderEnabled();
    const bool show_panel = (k == &DD::Image::Knob::showPanel);

    if (show_panel ||
        k->name() == "read_from_file"  ||
        k->name() == "lock_read_frame" ||
        k->name() == "lock_read_view")
    {
        enableSceneLoaderKnobs(scene_loader_enabled);
        enableSceneLoaderExtraKnobs(!scene_loader_enabled);
        updateSceneGraph();

        call_again = 1; // we want to be called again

    }
    else if (k->name() == "scene_file_version")
    {
        // Force a reload of the scene file. This usually means
        // invalidating any scene file caching to force the file
        // to be re-read:

        std::string file_path, plugin_type;
        _buildFilePathAndPluginType(getSceneFilePath().c_str(),
                                    "IO"/*plugin_class*/,
                                    file_path,
                                    plugin_type);

        if (!file_path.empty() && !plugin_type.empty())
        {
            // Execute but only send a cache-invalidate command:
            Fsr::NodeContext node_ctx;
            Fsr::NodeContext exec_ctx;
            {
                //--------------------------------------------------------------
                // Parameters to control creation of the Fuser execution node:
                node_ctx.setString(Arg::node_directive, Arg::Scene::file_archive_invalidate);
                node_ctx.setString(Arg::Scene::file,    file_path);

                //--------------------------------------------------------------
                // Parameters then passed to execute(), validateState(), and _execute():
                exec_ctx.setString(Arg::Scene::file,             file_path);
                exec_ctx.setBool(Arg::Scene::file_archive_debug, k_scene_ctls.archive_debug);
            }
            Fsr::Node::executeImmediate(plugin_type.c_str(),            /*node_class*/
                                        node_ctx.args(),                /*node_attribs*/
                                        NULL,                           /*node-parent*/
                                        exec_ctx,                       /*target_context*/
                                        Fsr::SceneArchiveContext::name  /*target_name*/);

            updateSceneGraph();
        }

        call_again = 1; // we want to be called again

    }
#if 0
    else if (k->name() == "force_load_button")
    {
        std::cout << "SceneLoader::knobChanged('" << k->name() << "')" << std::endl;
        updateSceneNode(true/*force_update*/, true/*force_load*/);
        call_again = 1;
    }
#endif
    else if (k->name() == "scene_file" ||
             k->name() == "scene_file_hierarchy" ||
             k->name() == "scenegraph_max_depth")
    {
        // Possibly update the node path if the user is doing this
        // change in the gui:
        if (k->name() == "scene_file" && k->isVisible())
            checkForValidNodePath();

        updateSceneGraph();

        call_again = 1; // we want to be called again

    }
#if 0
    else if (k->name() == "lock_button")
    {
        //std::cout << "Lock" << std::endl;
        k2 = op->knob("read_from_file"); if (k2) k2->set_value(1.0);
        k2 = op->knob("editable"); if (k2) k2->set_value(0.0);

        enableSceneLoaderKnobs(true);
        enableSceneLoaderExtraKnobs(true);

        call_again = 1;
    }
    else if (k->name() == "unlock_button")
    {
        //std::cout << "Unlock" << std::endl;
        k2 = op->knob("read_from_file"); if (k2) k2->set_value(0.0);
        k2 = op->knob("editable"); if (k2) k2->set_value(1.0);

        enableSceneLoaderKnobs(false);
        enableSceneLoaderExtraKnobs(false);

        call_again = 1;
    }
#endif
    else if (k == kSceneView)
    {
        // User selected an item in the scene graph, grab the string
        // and set the scene node path to it.

        DD::Image::SceneView_KnobI* scene_knob = kSceneView->sceneViewKnob();
        assert(scene_knob); // shouldn't happen...

        std::string item = scene_knob->getHighlightedItem();
        //std::cout << "  selected scene item '" << item << "'" << std::endl;
        if (!item.empty())
        {
            // Trim off a trailing '(<class>)' or trailing '/':
            char* p = const_cast<char*>(item.c_str());
            while (*p && *p != ' ' && *p != '(')
                ++p;
            // If item ends in a '/' then it's not a selectable object:
            if (p > item.c_str() && *(p-1) != '/')
            {
                *p = 0;
                std::string node_path; node_path.reserve(item.size()+1);
                node_path = "/";
                node_path += item.c_str();
                //std::cout << "    node_path '" << node_path << "'" << std::endl;
                op->knob("scene_node")->set_text(node_path.c_str());
            }
        }

        call_again = 1;

    }

    return call_again;
}


//------------------------------------------------------------------------------------


/*! Call this from owner Op::_validate(). Sets an error on owner Op if there's a loader error.
*/
/*virtual*/ void
SceneLoader::validateSceneLoader(bool /*for_real*/)
{
    if (m_loader_error)
        sceneOp()->error("%s", m_loader_error_msg.c_str());
}


//------------------------------------------------------------------------------------


/*! Returns the file path to the scene file, or an empty string if
    'scene_read_enabled' is off.

    TODO: check that scene_file expressions are evaluated here...!
          May need to use the get_text() method instead...
*/
std::string
SceneLoader::getSceneFilePath()
{
    DD::Image::Op* op = sceneOp();
#ifdef DEBUG
    assert(op);
#endif

    std::string file;
    if (isSceneLoaderEnabled())
    {
        DD::Image::Hash hash;
        DD::Image::Knob* k = op->knob("scene_file");
#if 1
        const char* s = "";
        if (k) k->store(DD::Image::StringPtr, &s, hash, op->outputContext());
        file = s;
#else
        if (sceneOp()->script_expand(k->get_text(&op->outputContext())) && op->script_result())
            file = op->script_result();
        op->script_unlock();
#endif
    }
    return file;
}


/*! Returns the scene path for the selected node, or an empty string if
    'scene_read_enabled' is off.
*/
std::string
SceneLoader::getSceneNodePath()
{
    DD::Image::Op* op = sceneOp();
#ifdef DEBUG
    assert(op);
#endif

    std::string node_path;
    if (isSceneLoaderEnabled())
    {
        DD::Image::Hash hash;
        DD::Image::Knob* k = op->knob("scene_node");

#if 1
        // TODO: check that scene_node tcl expressions are evaluated here...!
        //   May need to use the get_text() method instead...
        const char* s = "";
        if (k)
            k->store(DD::Image::StringPtr, &s, hash, op->outputContext());
        node_path = s;
#else
        if (sceneOp()->script_expand(k->get_text(&op->outputContext())) && op->script_result())
            node_path = op->script_result();
        op->script_unlock();
#endif

        // TODO: support custom expressions in the scene_node string such as
        // a '<first-valid>' directive to explicitly enable the auto-search
        // mode. Perhaps including the name of the node type like '<first-camera>'
        // or '<first-xform>'
        if (node_path.find("<first-valid>") != std::string::npos)
        {
            //do something
        }
        else if (node_path.find("<first-camera>") != std::string::npos)
        {
            //do something
        }

    }
    return node_path;
}


/*! Possibly load a scene node into the sceneOp() using values at current outputContext().
    If force_update == true then the hash is *always* updated and may cause a load.
    If force_load == true then loadSceneNode() is *always* called.
*/
void
SceneLoader::updateSceneNode(bool force_update,
                             bool force_load)
{
    DD::Image::Hash hash;
    updateSceneNode(hash, NULL/*context*/, force_update, force_load);
}


/*! This should be called from an implemented Op::append(Hash&) method.
    See note in class description for more info.

    This method will cause the scene node to be loaded on initial script
    load, or if knob values that affect the load state change.

    If force_update == true then the hash is *always* updated and may cause a load.
    If force_load == true then loadSceneNode() is *always* called.
*/
void
SceneLoader::updateSceneNode(DD::Image::Hash&                hash,
                             const DD::Image::OutputContext* context,
                             bool                            force_update,
                             bool                            force_load)
{
    // Check file/node loading hash state:
    DD::Image::Hash load_hash;
    SceneControls scene_ctrls;
    if (!context)
    {
        // No explicit context, use the ones stored in addSceneLoaderKnobs():
        memcpy(&scene_ctrls, &k_scene_ctls, sizeof(SceneControls));
    }
    else
    {
        // Store each knob for an explicit context:
        DD::Image::Op* op = sceneOp();
        DD::Image::Knob* k;
        k = op->knob("read_from_file"       ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.read_enabled,          hash, *context);
        k = op->knob("scene_file"           ); if (k) k->store(DD::Image::StringPtr, &scene_ctrls.file,                  hash, *context);
        k = op->knob("scene_file_version"   ); if (k) k->store(DD::Image::IntPtr,    &scene_ctrls.file_version,          hash, *context);
        //
        k = op->knob("scene_node"           ); if (k) k->store(DD::Image::StringPtr, &scene_ctrls.node_path,             hash, *context);
        //
        k = op->knob("decompose_xform_order"); if (k) k->store(DD::Image::IntPtr,    &scene_ctrls.decompose_xform_order, hash, *context);
        k = op->knob("decompose_rot_order"  ); if (k) k->store(DD::Image::IntPtr,    &scene_ctrls.decompose_rot_order,   hash, *context);
        k = op->knob("translate_enable"     ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.T_enable,              hash, *context);
        //
        k = op->knob("translate_enable"     ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.T_enable,              hash, *context);
        k = op->knob("rotate_enable"        ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.R_enable,              hash, *context);
        k = op->knob("scale_enable"         ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.S_enable,              hash, *context);
        k = op->knob("euler_filter_enable"  ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.euler_filter_enable,   hash, *context);
        k = op->knob("parent_extract_enable"); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.parent_extract_enable, hash, *context);
        //
        k = op->knob("lock_read_frame"      ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.lock_read_frame,       hash, *context);
        k = op->knob("read_frame"           ); if (k) k->store(DD::Image::DoublePtr, &scene_ctrls.read_frame,            hash, *context);
        k = op->knob("frame_offset"         ); if (k) k->store(DD::Image::DoublePtr, &scene_ctrls.frame_offset,          hash, *context);
        k = op->knob("frame_origin"         ); if (k) k->store(DD::Image::DoublePtr, &scene_ctrls.frame_origin,          hash, *context);
        k = op->knob("frames_per_second"    ); if (k) k->store(DD::Image::DoublePtr, &scene_ctrls.frames_per_second,     hash, *context);
        k = op->knob("lock_read_view"       ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.lock_read_view,        hash, *context);
        k = op->knob("read_view"            ); if (k) k->store(DD::Image::IntPtr,    &scene_ctrls.read_view,             hash, *context);
        //
        k = op->knob("scene_read_debug"     ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.read_debug,            hash, *context);
        k = op->knob("scene_archive_debug"  ); if (k) k->store(DD::Image::BoolPtr,   &scene_ctrls.archive_debug,         hash, *context);
    }

    load_hash.append(scene_ctrls.read_enabled);
    load_hash.append(scene_ctrls.file);
    load_hash.append(scene_ctrls.file_version);
    //
    // TODO: support evaluation of expressions in node_path here so that the hash
    // is reflective of the evaluated result:
    load_hash.append(scene_ctrls.node_path);
    //
    load_hash.append(scene_ctrls.decompose_xform_order);
    load_hash.append(scene_ctrls.decompose_rot_order  );
    //
    load_hash.append(scene_ctrls.T_enable);
    load_hash.append(scene_ctrls.R_enable);
    load_hash.append(scene_ctrls.S_enable);
    load_hash.append(scene_ctrls.euler_filter_enable  );
    load_hash.append(scene_ctrls.parent_extract_enable);
    //
    load_hash.append(scene_ctrls.lock_read_frame  );
    load_hash.append(scene_ctrls.read_frame       );
    load_hash.append(scene_ctrls.frame_offset     );
    load_hash.append(scene_ctrls.frame_origin     );
    load_hash.append(scene_ctrls.frames_per_second);
    load_hash.append(scene_ctrls.lock_read_view   );
    load_hash.append(scene_ctrls.read_view        );
    //
    load_hash.append(scene_ctrls.read_debug   );
    load_hash.append(scene_ctrls.archive_debug);

    hash.append(load_hash);

    //std::cout << "SceneLoader('" << sceneOp()->node_name() << "' " << this << ")::updateSceneNode():";
    //std::cout << " scene_file='" << scene_ctrls.file << "', scene_node='" << scene_ctrls.node_path << "'";
    //std::cout << ", scene_read_enabled=" << scene_ctrls.read_enabled;
    //std::cout << ", load_hash=0x" << std::hex << load_hash.value() << std::dec;
    //std::cout << std::endl;

    if (force_update || force_load || m_load_hash != load_hash)
    {
        m_load_hash = load_hash;
        clearLoadError();
        if (force_load || scene_ctrls.read_enabled)
        {
            if (!loadSceneNode(scene_ctrls))
                sceneOp()->error("%s", m_loader_error_msg.c_str());
        }
    }
    else if (m_loader_error)
    {
        sceneOp()->error("%s", m_loader_error_msg.c_str());
    }
}


/*! This should be called from an implemented Op::append(Hash&) method.
    See note in class description for more info.

    This method will cause the scene node to be loaded on initial script
    load.
*/
bool
SceneLoader::loadSceneNode(const SceneControls& scene_ctrls)
{
    if (scene_ctrls.read_debug)
    {
        std::cout << "SceneLoader('" << sceneOp()->node_name() << "' " << this << ")::loadSceneNode():";
        std::cout << " read_enabled=" << scene_ctrls.read_enabled;
        std::cout << ", scene_file='" << scene_ctrls.file << "', scene_node='" << scene_ctrls.node_path << "'";
        std::cout << std::endl;
    }

    if (!readSceneNode(scene_ctrls.file,
                       scene_ctrls.node_path,
                       scene_ctrls.read_debug))
    {
        // Error reading node
        return false;
    }

    // Disable the editable switch so it's harder for users to mess up the data:
    if (this->asAxisOp())
    {
        DD::Image::Knob* k = sceneOp()->knob("editable" );
        if (k) k->set_value(0.0);
    }

    return true;
}


/*! Update the scenegraph display knob (gets the node descriptions first.)
*/
void
SceneLoader::updateSceneGraph()
{
    //std::cout << "SceneLoader('" << sceneOp()->node_name() << "' " << this << ")::updateSceneGraph():";
    if (!kSceneView || !kSceneView->isVisible())
        return;

    // Get the list of nodes for the scenegraph knob:
    Fsr::NodeDescriptionMap node_descriptions;
    if (getNodeDescriptions(getSceneFilePath().c_str(),
                            node_descriptions,
                            k_scene_ctls.read_debug))
        updateSceneGraph(node_descriptions);
    else
        updateSceneGraph(node_descriptions, "<error loading scene file>"); // error
}


/*! Update the enumeration knob pullown with the list of scene file nodes.

    no_nodes_message string is used in the absence of any descriptions, for example to
    indicate an empty file or an error, which is put into fist line of list knob.

    TODO: this should change to a dedicated SceneGraphKnob class?
*/
void
SceneLoader::updateSceneGraph(const Fsr::NodeDescriptionMap& node_descriptions,
                              const char*                    no_nodes_message)
{
    //std::cout << "SceneLoader('" << sceneOp()->node_name() << "' " << this << ")::updateSceneGraph():";
    //std::cout << std::endl;
    if (!kSceneView || !kSceneView->isVisible())
        return;

    std::vector<std::string> menu_list;  //!< List of node name strings for pulldown menu
    menu_list.reserve(node_descriptions.size());

    char path[2048];

    // Create the name list for the menu:
    const size_t nDesc = node_descriptions.size();
    //int count = 0;
    for (Fsr::NodeDescriptionMap::const_iterator it=node_descriptions.begin(); it != node_descriptions.end(); ++it)
    {
        //std::cout << "  '" << it->first << "':'" << it->second.type << "'" << std::endl;
        const std::string& desc_id = it->first;
        if ((desc_id.empty() || desc_id == "/") && nDesc > 1)
            continue; // skip root if there's more nodes

        const NodeDescription& desc = it->second;
        if (desc.type.empty())
        {
            if (desc.note == "PATH_TRUNCATED")
                snprintf(path, 2048, "%s ...", desc_id.c_str());
            else if (!desc.note.empty())
                snprintf(path, 2048, "%s (%s)", desc_id.c_str(), desc.note.c_str());
            else
                snprintf(path, 2048, "%s", desc_id.c_str());
        }
        else
        {
            if (desc.note == "PATH_TRUNCATED")
                snprintf(path, 2048, "%s  (%s) ...", desc_id.c_str(), desc.type.c_str());
            else if (!desc.note.empty())
                snprintf(path, 2048, "%s (%s)  (%s)", desc_id.c_str(), desc.note.c_str(), desc.type.c_str());
            else
                snprintf(path, 2048, "%s  (%s)", desc_id.c_str(), desc.type.c_str());
        }
        menu_list.push_back(std::string(path));
    }
    // Sort the node paths alphabetically:
    //std::sort(menu_list.begin(), menu_list.end());

    if (menu_list.size() == 0)
    {
        if (no_nodes_message && no_nodes_message[0])
            menu_list.push_back(std::string(no_nodes_message));
        else
            menu_list.push_back(std::string("<empty scene file>")); // default message
    }

    DD::Image::SceneView_KnobI* scene_knob = kSceneView->sceneViewKnob();
    assert(scene_knob); // shouldn't happen...

    //std::cout << "  loading scene view knob" << std::endl;
    scene_knob->setSelectedItems(std::vector<unsigned int>()); // clear any existing selection
    scene_knob->menu(std::vector<std::string>()); // clear any existing items
    scene_knob->addItems(menu_list);
    scene_knob->setColumnHeader("contents");//"file node hierarchy");
    scene_knob->setSelectionMode(DD::Image::SceneView_KnobI::eSelectionModeHighlight);
    //scene_knob->autoExpand(false);
    scene_knob->viewAllNodes(true);

}


/*!
*/
/*virtual*/
void
SceneLoader::enableSceneLoaderKnobs(bool scene_loader_enabled)
{
    DD::Image::Op* op = sceneOp();
    DD::Image::Knob* k;
    k = op->knob("scene_file"      ); if (k) k->enable(scene_loader_enabled);
    k = op->knob("scene_file_nodes"); if (k) k->enable(scene_loader_enabled);
    k = op->knob("scene_node"      ); if (k) k->enable(scene_loader_enabled);

    k = op->knob("read_frame"  ); if (k) k->enable(k_scene_ctls.lock_read_frame);
    k = op->knob("frame_origin"); if (k) k->enable(!k_scene_ctls.lock_read_frame);
    k = op->knob("frame_rate"  ); if (k) k->enable(!k_scene_ctls.lock_read_frame);

    k = op->knob("read_view"); if (k) k->enable(k_scene_ctls.lock_read_view);
}


/*!
*/
/*virtual*/
void
SceneLoader::enableSceneLoaderExtraKnobs(bool read_enabled)
{
    // base class does nothing
}


/*! Enable the loader error state and fill in the error msg string.
    If already in error state this returns fast without affecting
    error message contents.
*/
void
SceneLoader::setLoadError(const char* msg, ...)
{
    if (m_loader_error)
        return; // error state already set

    // Expand the va list:
    char buf[2048];
    va_list args;
    va_start(args, msg);
    vsnprintf(buf, 2048, msg, args);
    va_end(args);

    m_loader_error = true;
    m_loader_error_msg = buf;
}


/*! Disable the error state and clear the error message.
*/
void
SceneLoader::clearLoadError()
{
    m_loader_error = false;
    m_loader_error_msg.clear();
}


/*!
*/
void
SceneLoader::showUserWarning(const char* msg, ...)
{
    /*  'i' will show a message dialog
        '!' will show an alert dialog
        '?' will show a question and return 1 if the user click 'yes', and 0 otherwise
    */
    va_list args;
    va_start(args, msg);
    DD::Image::Op::message_vf('!', msg, args);
    va_end(args);
}


//------------------------------------------------------------------------------------


/*! Get a list of node descriptions from the scene file.

    
*/
bool
SceneLoader::getNodeDescriptions(const char*              file,
                                 Fsr::NodeDescriptionMap& node_descriptions,
                                 bool                     debug)
{
    node_descriptions.clear();
    if (debug)
    {
        std::cout << "SceneLoader('" << sceneOp()->node_name() << "' " << this << ")::getNodeDescriptions()";
        std::cout << " file='" << file << "'";
        std::cout << std::endl;
    }

    // Update file name and type (strip leading extension off filename and extract extension string):
    std::string file_path, plugin_type;
    _buildFilePathAndPluginType(file, "IO"/*plugin_class*/, file_path, plugin_type);
    if (debug)
        std::cout << "  plugin_type='" << plugin_type << "'" << std::endl;

    // Don't bother if path or plugin name are empty:
    if (file_path.empty() || plugin_type.empty())
    {
        if (debug)
            std::cout << "  warning, unable to read nodes" << std::endl;
        return false;
    }

    DD::Image::Op* op = sceneOp();
#ifdef DEBUG
    assert(op);
#endif

    uint32_t path_max_depth = SCENEGRAPH_MAX_DEPTH_DEFAULT;
    if (op->knob("scenegraph_max_depth"))
        path_max_depth = std::max(1, int(op->knob("scenegraph_max_depth")->get_value()));

    // TODO: add scope path knob like in GeoSceneGraphReader?
    const char* start_path_at  = "/";

    // Build context (args) to pass to FuserPrims ctors:
    Fsr::NodeContext node_ctx;
    Fsr::NodeContext exec_ctx;
    {
        //--------------------------------------------------------------
        // Parameters to control creation of the Fuser execution node:
        node_ctx.setString(Arg::node_directive,            Arg::Scene::node_type_contents);
        node_ctx.setString(Arg::Scene::file,               file_path);
        node_ctx.setString(Arg::Scene::path,               "/"); // primary node path is root(the archive) in this case
        node_ctx.setBool(  Arg::Scene::read_debug,         k_scene_ctls.read_debug);
        node_ctx.setBool(  Arg::Scene::file_archive_debug, k_scene_ctls.archive_debug);

        //--------------------------------------------------------------
        // Parameters then passed to execute(), validateState(), and _execute():
        exec_ctx.setString(Arg::Scene::path,               (start_path_at) ? start_path_at : "/");
        exec_ctx.setInt(   Arg::Scene::path_max_depth,     path_max_depth);
        exec_ctx.setBool(  Arg::Scene::read_debug,         k_scene_ctls.read_debug);
        exec_ctx.setBool(  Arg::Scene::file_archive_debug, k_scene_ctls.archive_debug);
    }

    Fsr::ScenePathFilters scene_path_filters;
    scene_path_filters.node_filter_patterns = NULL;
    //
    Fsr::SceneNodeDescriptions scene_node_descriptions;
    scene_node_descriptions.node_description_map = &node_descriptions;

    Fsr::Node::ErrCtx err = Fsr::Node::executeImmediate(plugin_type.c_str(),          /*node_class*/
                                                        node_ctx.args(),              /*node_args*/
                                                        NULL,                         /*node-parent*/
                                                        exec_ctx,                     /*target_context*/
                                                        scene_node_descriptions.name, /*target_name*/
                                                        &scene_node_descriptions,     /*target*/
                                                        &scene_path_filters           /*src0*/);
    if (err.state == -1)
        return true; // user-abort
    else if (err.state == -2)
    {
        //if (err == -1)
        //    showUserWarning("SceneLoader::getNodeDescriptions('%s') error, unknown file type '%s'",
        //                    file_path.c_str(), plugin_type.c_str());
        if (debug)
        {
            std::cerr << "SceneLoader::getNodeDescriptions('" << file << "')";
            std::cerr << " error '" << err.msg << "'" << std::endl;
        }
        return false;
    }

    return true;
}


//------------------------------------------------------------------------------------


/*!
*/
std::string
SceneLoader::findDefaultNode(const char* file,
                             bool        debug)
{
    if (debug)
    {
        std::cout << "SceneLoader('" << sceneOp()->node_name() << "' " << this << ")::findFirstDefaultNode()";
        std::cout << ", file='" << file << "'";
        std::cout << std::endl;
    }

    // Update file name and type (strip leading extension off filename and extract extension string):
    std::string scene_file_path, fuser_plugin_type;
    _buildFilePathAndPluginType(file, "IO"/*plugin_class*/, scene_file_path, fuser_plugin_type);
    if (debug)
        std::cout << "  plugin_type='" << fuser_plugin_type << "'" << std::endl;

    // Don't bother if path or plugin name are empty:
    if (scene_file_path.empty() || fuser_plugin_type.empty())
    {
        if (debug)
            std::cout << "  warning, unable to search for default node" << std::endl;
        return std::string();
    }

    return _findDefaultNode(scene_file_path,
                            fuser_plugin_type,
                            std::string((defaultSceneNodeType()) ? defaultSceneNodeType() : "xform"),
                            debug);
}


/*!
*/
/*virtual*/ std::string
SceneLoader::_findDefaultNode(const std::string& scene_file_path,
                              const std::string& fuser_plugin_type,
                              const std::string& default_node_type,
                              bool               debug)
{
    if (default_node_type.empty())
    {
        if (debug)
            std::cout << "  warning, unable to search for a default node, default type is empty" << std::endl;
        return std::string();
    }

    // TODO: update this from a knob?
    const char* start_path_at  = "/";

    // Build context (args) to pass to FuserPrims ctors:
    Fsr::NodeContext node_ctx;
    Fsr::NodeContext exec_ctx;
    {
        //--------------------------------------------------------------
        // Parameters to control creation of the Fuser execution node:
        node_ctx.setString(Arg::node_directive,            Arg::Scene::node_find_first_valid);
        node_ctx.setString(Arg::Scene::file,               scene_file_path);
        node_ctx.setString(Arg::Scene::path,               "/"); // primary node path is root(the archive) in this case
        node_ctx.setBool(  Arg::Scene::read_debug,         k_scene_ctls.read_debug);
        node_ctx.setBool(  Arg::Scene::file_archive_debug, k_scene_ctls.archive_debug);

        //--------------------------------------------------------------
        // Parameters then passed to execute(), validateState(), and _execute():
        exec_ctx.setString(Arg::Scene::path,               (start_path_at) ? start_path_at : "/");
        exec_ctx.setString(Arg::Scene::node_type,          default_node_type);
        exec_ctx.setBool(  Arg::Scene::read_debug,         k_scene_ctls.read_debug);
        exec_ctx.setBool(  Arg::Scene::file_archive_debug, k_scene_ctls.archive_debug);
    }

    Fsr::NodeDescriptionMap found_nodes;
    //
    Fsr::SceneNodeDescriptions search_ctx;
    search_ctx.node_description_map = &found_nodes;

    Fsr::Node::ErrCtx err = Fsr::Node::executeImmediate(fuser_plugin_type.c_str(),        /*node_class*/
                                                        node_ctx.args(),                  /*node_args*/
                                                        NULL,                             /*node-parent*/
                                                        exec_ctx,                         /*target_context*/
                                                        Fsr::SceneNodeDescriptions::name, /*target_name*/
                                                        &search_ctx                       /*target*/);
    // Set load error on execute failure, but not on user-abort:
    if (err.state == -1)
        return std::string();
    else if (err.state == -2)
    {
        setLoadError("SceneLoader: error '%s' trying to read file '%s'", err.msg.c_str(), scene_file_path.c_str());
        if (debug)
            std::cerr << "SceneLoader: error '" << err.msg << "' trying to read file '" << scene_file_path << "'" << std::endl;
        return std::string();
    }

    if (found_nodes.size() > 0)
    {
        //std::cout << "   match='" << found_nodes.begin()->first << "'" << std::endl;
        return found_nodes.begin()->first;
    }

    return std::string();
}


//------------------------------------------------------------------------------------


/*!
*/
bool
SceneLoader::readSceneNode(const char* file,
                           const char* node_path,
                           bool        debug)
{
    if (debug)
    {
        std::cout << "SceneLoader('" << sceneOp()->node_name() << "' " << this << ")::readSceneNode('" << node_path << "')";
        std::cout << ", file='" << file << "'";
        std::cout << std::endl;
    }

    // Update file name and type (strip leading extension off filename and extract extension string):
    std::string scene_file_path, fuser_plugin_type;
    _buildFilePathAndPluginType(file, "IO"/*plugin_class*/, scene_file_path, fuser_plugin_type);
    if (debug)
        std::cout << "  plugin_type='" << fuser_plugin_type << "'" << std::endl;

    // Don't bother if path or plugin name are empty:
    if (scene_file_path.empty() || fuser_plugin_type.empty())
    {
        if (debug)
            std::cout << "  warning, unable to read node" << std::endl;
        return false;
    }


    if (!node_path || !node_path[0])
    {
        setLoadError("SceneLoader: empty node path");
        if (debug)
            std::cerr << "SceneLoader: empty node path" << std::endl;
        return false;
    }

    //
    return _readSceneNode(scene_file_path,
                          node_path,
                          fuser_plugin_type,
                          debug);
}


/*! Implementation-specific readSceneNode() method.
    Base class executes the Fuser IO module.
*/
/*virtual*/ bool
SceneLoader::_readSceneNode(const std::string& scene_file_path,
                            const std::string& expanded_node_path,
                            const std::string& fuser_plugin_type,
                            bool               debug)
{
    Fsr::NodeContext node_ctx; // Context (args) to pass to FuserPrims ctors
    Fsr::NodeContext exec_ctx; // Context (args) to pass to execute() method
    {
        //--------------------------------------------------------------
        // Parameters to control creation of the Fuser execution node:
        node_ctx.setString(Arg::node_directive,            Arg::Scene::node_type_auto);
        node_ctx.setString(Arg::Scene::file,               scene_file_path);
        node_ctx.setString(Arg::Scene::path,               expanded_node_path);
        node_ctx.setBool(  Arg::Scene::read_debug,         k_scene_ctls.read_debug);
        node_ctx.setBool(  Arg::Scene::file_archive_debug, k_scene_ctls.archive_debug);

        //--------------------------------------------------------------
        // Parameters then passed to execute(), validateState(), and _execute():
        exec_ctx.setBool(  "reader:lock_read_frame", k_scene_ctls.lock_read_frame  );
        exec_ctx.setDouble("reader:read_frame",      k_scene_ctls.read_frame       );
        exec_ctx.setDouble("reader:frame_offset",    k_scene_ctls.frame_offset     );
        exec_ctx.setDouble("reader:frame_origin",    k_scene_ctls.frame_origin     );
        exec_ctx.setDouble("reader:fps",             k_scene_ctls.frames_per_second);
        //
        exec_ctx.setBool(  "reader:lock_read_view",  k_scene_ctls.lock_read_view   );
        exec_ctx.setString("reader:read_view",       DD::Image::OutputContext::viewname(k_scene_ctls.read_view));
        //
        exec_ctx.setInt( Arg::Scene::decompose_xform_order, k_scene_ctls.decompose_xform_order);
        exec_ctx.setInt( Arg::Scene::decompose_rot_order,   k_scene_ctls.decompose_rot_order  );
        exec_ctx.setBool(Arg::Scene::T_enable,              k_scene_ctls.T_enable             );
        exec_ctx.setBool(Arg::Scene::R_enable,              k_scene_ctls.R_enable             );
        exec_ctx.setBool(Arg::Scene::S_enable,              k_scene_ctls.S_enable             );
        exec_ctx.setBool(Arg::Scene::euler_filter_enable,   k_scene_ctls.euler_filter_enable  );
        exec_ctx.setBool(Arg::Scene::parent_extract_enable, k_scene_ctls.parent_extract_enable);
        exec_ctx.setBool(Arg::Scene::read_debug,            k_scene_ctls.read_debug           );
        exec_ctx.setBool(Arg::Scene::file_archive_debug,    k_scene_ctls.archive_debug);
    }

    Fsr::SceneOpImportContext scene_op_ctx(sceneOp(),
                                           sceneOp()->outputContext());

    Fsr::Node::ErrCtx err = Fsr::Node::executeImmediate(fuser_plugin_type.c_str(),       /*node_class*/
                                                        node_ctx.args(),                 /*node_args*/
                                                        NULL,                            /*node-parent*/
                                                        exec_ctx,                        /*target_context*/
                                                        Fsr::SceneOpImportContext::name, /*target_name*/
                                                        &scene_op_ctx                    /*target*/);
    // Set load error on execute failure, but not on user-abort:
    if (err.state == -1)
        return true; // user-abort
    else if (err.state == -2)
    {
        setLoadError("SceneLoader: error '%s' trying to read file '%s'", err.msg.c_str(), scene_file_path.c_str());
        if (debug)
            std::cerr << "SceneLoader: error '" << err.msg << "' trying to read file '" << scene_file_path << "'" << std::endl;
        return false;
    }

    return true;
}


} // namespace Fsr


// end of FuserSceneLoader.cpp


//
// Copyright 2019 DreamWorks Animation
//
