/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "ipde_gameplay_nodes.h"
#include "ipde_nodes.h"

#include "core/object/class_db.h"

void initialize_ipde_nodes_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(FPSCounter);
		GDREGISTER_CLASS(AutoRotate3D);
		GDREGISTER_CLASS(AutoRotate2D);
		GDREGISTER_CLASS(Bobbing3D);
		GDREGISTER_ABSTRACT_CLASS(ScreenShakeBase);
		GDREGISTER_CLASS(ScreenShake2D);
		GDREGISTER_CLASS(ScreenShake3D);
		GDREGISTER_CLASS(HealthComponent);
		GDREGISTER_CLASS(Lifetime);
		GDREGISTER_CLASS(Bobbing2D);
		GDREGISTER_CLASS(FollowTarget2D);
		GDREGISTER_CLASS(FollowTarget3D);
		GDREGISTER_CLASS(Spawner2D);
		GDREGISTER_CLASS(Spawner3D);
	}
}

void uninitialize_ipde_nodes_module(ModuleInitializationLevel p_level) {
}
