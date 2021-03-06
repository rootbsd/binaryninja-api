// Copyright (c) 2015-2016 Vector 35 LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include <stdio.h>
#include <inttypes.h>
#include "binaryninjaapi.h"

using namespace BinaryNinja;
using namespace std;


class GeneratorArchitecture: public Architecture
{
public:
	GeneratorArchitecture(): Architecture("generator")
	{
	}

	virtual bool GetInstructionInfo(const uint8_t*, uint64_t, size_t, InstructionInfo&) override
	{
		return false;
	}

	virtual bool GetInstructionText(const uint8_t*, uint64_t, size_t&, vector<InstructionTextToken>&) override
	{
		return false;
	}

	virtual BNEndianness GetEndianness() const override
	{
		return LittleEndian;
	}

	virtual size_t GetAddressSize() const override
	{
		return 8;
	}
};


void OutputType(FILE* out, Type* type, bool isReturnType = false, bool isCallback = false)
{
	switch (type->GetClass())
	{
	case BoolTypeClass:
		fprintf(out, "ctypes.c_bool");
		break;
	case IntegerTypeClass:
		switch (type->GetWidth())
		{
		case 1:
			if (type->IsSigned())
				fprintf(out, "ctypes.c_byte");
			else
				fprintf(out, "ctypes.c_ubyte");
			break;
		case 2:
			if (type->IsSigned())
				fprintf(out, "ctypes.c_short");
			else
				fprintf(out, "ctypes.c_ushort");
			break;
		case 4:
			if (type->IsSigned())
				fprintf(out, "ctypes.c_int");
			else
				fprintf(out, "ctypes.c_uint");
			break;
		default:
			if (type->IsSigned())
				fprintf(out, "ctypes.c_longlong");
			else
				fprintf(out, "ctypes.c_ulonglong");
			break;
		}
		break;
	case FloatTypeClass:
		if (type->GetWidth() == 4)
			fprintf(out, "ctypes.c_float");
		else
			fprintf(out, "ctypes.c_double");
		break;
	case StructureTypeClass:
		fprintf(out, "%s", type->GetQualifiedName(type->GetStructure()->GetName()).c_str());
		break;
	case EnumerationTypeClass:
		fprintf(out, "%s", type->GetQualifiedName(type->GetEnumeration()->GetName()).c_str());
		break;
	case PointerTypeClass:
		if (isCallback || (type->GetChildType()->GetClass() == VoidTypeClass))
		{
			fprintf(out, "ctypes.c_void_p");
			break;
		}
		else if ((type->GetChildType()->GetClass() == IntegerTypeClass) &&
		         (type->GetChildType()->GetWidth() == 1) && (type->GetChildType()->IsSigned()))
		{
			if (isReturnType)
				fprintf(out, "ctypes.POINTER(ctypes.c_byte)");
			else
				fprintf(out, "ctypes.c_char_p");
			break;
		}
		else if (type->GetChildType()->GetClass() == FunctionTypeClass)
		{
			fprintf(out, "ctypes.CFUNCTYPE(");
			OutputType(out, type->GetChildType()->GetChildType(), true, true);
			for (auto& i : type->GetChildType()->GetParameters())
			{
				fprintf(out, ", ");
				OutputType(out, i.type);
			}
			fprintf(out, ")");
			break;
		}
		fprintf(out, "ctypes.POINTER(");
		OutputType(out, type->GetChildType());
		fprintf(out, ")");
		break;
	case ArrayTypeClass:
		OutputType(out, type->GetChildType());
		fprintf(out, " * %" PRId64, type->GetElementCount());
		break;
	default:
		fprintf(out, "None");
		break;
	}
}


int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		fprintf(stderr, "Usage: generator <header> <output>\n");
		return 1;
	}

	Architecture::Register(new GeneratorArchitecture());

	// Parse API header to get type and function information
	map<string, Ref<Type>> types, vars, funcs;
	string errors;
	bool ok = Architecture::GetByName("generator")->ParseTypesFromSourceFile(argv[1], types, vars, funcs, errors);
	fprintf(stderr, "%s", errors.c_str());
	if (!ok)
		return 1;

	FILE* out = fopen(argv[2], "w");

	fprintf(out, "import ctypes, os\n\n");

	fprintf(out, "# Load core module\n");
#if defined(__APPLE__)
	fprintf(out, "_base_path = os.path.join(os.path.dirname(__file__), \"..\", \"..\", \"..\", \"MacOS\")\n");
#else
	fprintf(out, "_base_path = os.path.join(os.path.dirname(__file__), \"..\", \"..\")\n");
#endif

#ifdef WIN32
	fprintf(out, "core = ctypes.CDLL(os.path.join(_base_path, \"binaryninjacore.dll\"))\n\n");
#elif defined(__APPLE__)
	fprintf(out, "core = ctypes.CDLL(os.path.join(_base_path, \"libbinaryninjacore.dylib\"))\n\n");
#else
	fprintf(out, "core = ctypes.CDLL(os.path.join(_base_path, \"libbinaryninjacore.so.1\"))\n\n");
#endif

	// Create type objects
	fprintf(out, "# Type definitions\n");
	map<string, int64_t> enumMembers;
	for (auto& i : types)
	{
		if (i.second->GetClass() == StructureTypeClass)
		{
			fprintf(out, "class %s(ctypes.Structure):\n", i.first.c_str());
			fprintf(out, "    pass\n");
		}
		else if (i.second->GetClass() == EnumerationTypeClass)
		{
			fprintf(out, "%s = ctypes.c_int\n", i.first.c_str());
			for (auto& j : i.second->GetEnumeration()->GetMembers())
				fprintf(out, "%s = %" PRId64 "\n", j.name.c_str(), j.value);
			fprintf(out, "%s_names = {\n", i.first.c_str());
			for (auto& j : i.second->GetEnumeration()->GetMembers())
				fprintf(out, "    %" PRId64 ": \"%s\",\n", j.value, j.name.c_str());
			fprintf(out, "}\n");
			fprintf(out, "%s_by_name = {\n", i.first.c_str());
			for (auto& j : i.second->GetEnumeration()->GetMembers())
				fprintf(out, "    \"%s\": %" PRId64 ",\n", j.name.c_str(), j.value);
			fprintf(out, "}\n");
			for (auto& j : i.second->GetEnumeration()->GetMembers())
				enumMembers[j.name] = j.value;
		}
		else if ((i.second->GetClass() == BoolTypeClass) || (i.second->GetClass() == IntegerTypeClass) ||
		         (i.second->GetClass() == FloatTypeClass) || (i.second->GetClass() == ArrayTypeClass))
		{
			fprintf(out, "%s = ", i.first.c_str());
			OutputType(out, i.second);
			fprintf(out, "\n");
		}
	}

	fprintf(out, "all_enum_values = {\n");
	for (auto& i : enumMembers)
		fprintf(out, "    \"%s\": %" PRId64 ",\n", i.first.c_str(), i.second);
	fprintf(out, "}\n");

	fprintf(out, "\n# Structure definitions\n");
	for (auto& i : types)
	{
		if ((i.second->GetClass() == StructureTypeClass) && (i.second->GetStructure()->GetMembers().size() != 0))
		{
			fprintf(out, "%s._fields_ = [\n", i.first.c_str());
			for (auto& j : i.second->GetStructure()->GetMembers())
			{
				fprintf(out, "        (\"%s\", ", j.name.c_str());
				OutputType(out, j.type);
				fprintf(out, "),\n");
			}
			fprintf(out, "    ]\n");
		}
	}

	fprintf(out, "\n# Function definitions\n");
	for (auto& i : funcs)
	{
		// Check for a string result, these will be automatically wrapped to free the string
		// memory and return a Python string
		bool stringResult = (i.second->GetChildType()->GetClass() == PointerTypeClass) &&
			(i.second->GetChildType()->GetChildType()->GetWidth() == 1) &&
			(i.second->GetChildType()->GetChildType()->IsSigned());
		// Pointer returns will be automatically wrapped to return None on null pointer
		bool pointerResult = (i.second->GetChildType()->GetClass() == PointerTypeClass);
		bool callbackConvention = false;
		if (i.first == "BNAllocString")
		{
			// Don't perform automatic wrapping of string allocation, and return a void
			// pointer so that callback functions (which is the only valid use of BNAllocString)
			// can properly return the result
			stringResult = false;
			callbackConvention = true;
		}

		string funcName = i.first;
		if (stringResult || pointerResult)
			funcName = string("_") + funcName;

		fprintf(out, "%s = core.%s\n", funcName.c_str(), i.first.c_str());
		fprintf(out, "%s.restype = ", funcName.c_str());
		OutputType(out, i.second->GetChildType(), true, callbackConvention);
		fprintf(out, "\n");
		if (!i.second->HasVariableArguments())
		{
			fprintf(out, "%s.argtypes = [\n", funcName.c_str());
			for (auto& j : i.second->GetParameters())
			{
				fprintf(out, "        ");
				if (i.first == "BNFreeString")
				{
					// BNFreeString expects a pointer to a string allocated by the core, so do not use
					// a c_char_p here, as that would be allocated by the Python runtime.  This can
					// be enforced by outputting like a return value.
					OutputType(out, j.type, true);
				}
				else
				{
					OutputType(out, j.type);
				}
				fprintf(out, ",\n");
			}
			fprintf(out, "    ]\n");
		}

		if (stringResult)
		{
			// Emit wrapper to get Python string and free native memory
			fprintf(out, "def %s(*args):\n", i.first.c_str());
			fprintf(out, "    result = %s(*args)\n", funcName.c_str());
			fprintf(out, "    string = ctypes.cast(result, ctypes.c_char_p).value\n");
			fprintf(out, "    BNFreeString(result)\n");
			fprintf(out, "    return string\n");
		}
		else if (pointerResult)
		{
			// Emit wrapper to return None on null pointer
			fprintf(out, "def %s(*args):\n", i.first.c_str());
			fprintf(out, "    result = %s(*args)\n", funcName.c_str());
			fprintf(out, "    if not result:\n");
			fprintf(out, "        return None\n");
			fprintf(out, "    return result\n");
		}
	}

	fprintf(out, "\n# Helper functions\n");
	fprintf(out, "def handle_of_type(value, handle_type):\n");
	fprintf(out, "    if isinstance(value, ctypes.POINTER(handle_type)) or isinstance(value, ctypes.c_void_p):\n");
	fprintf(out, "        return ctypes.cast(value, ctypes.POINTER(handle_type))\n");
	fprintf(out, "    raise ValueError, 'expected pointer to %%s' %% str(handle_type)\n");

	fprintf(out, "\n# Set path for core plugins\n");
	fprintf(out, "BNSetBundledPluginDirectory(os.path.join(_base_path, \"plugins\"))\n");

	fclose(out);
	return 0;
}
