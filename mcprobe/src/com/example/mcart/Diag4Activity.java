package com.example.mcart;

import org.lwjgl.PointerBuffer;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.libffi.FFICIF;
import org.lwjgl.system.libffi.FFIType;
import org.lwjgl.system.libffi.LibFFI;

/** libffi 可用性验证：版本、结构体布局、ffi_prep_cif。 */
public class Diag4Activity extends DiagActivity {

    @Override
    protected String tag() { return "diag4"; }

    @Override
    protected void runDiag() {
        mark("vm=" + System.getProperty("java.vm.name"));
        try {
            mark("FFI_VERSION_STRING = " + LibFFI.FFI_VERSION_STRING);
            mark("FFI_VERSION_NUMBER = " + LibFFI.FFI_VERSION_NUMBER);
            mark("FFI_DEFAULT_ABI    = " + LibFFI.FFI_DEFAULT_ABI + " (FFI_SYSV=" + LibFFI.FFI_SYSV + ")");
            mark("FFI_OK             = " + LibFFI.FFI_OK);
            mark("FFI_TYPE_SINT32    = " + LibFFI.FFI_TYPE_SINT32);
            mark("FFIType.ffi_type_sint32 sizeof = " + LibFFI.ffi_type_sint32.size()
                    + " type=" + LibFFI.ffi_type_sint32.type());
            mark("FFICIF.SIZEOF = " + FFICIF.SIZEOF + " ALIGNOF=" + FFICIF.ALIGNOF);
            mark("FFICIF.ABI=" + FFICIF.ABI + " NARGS=" + FFICIF.NARGS
                    + " ARG_TYPES=" + FFICIF.ARG_TYPES + " RTYPE=" + FFICIF.RTYPE
                    + " BYTES=" + FFICIF.BYTES + " FLAGS=" + FFICIF.FLAGS);
        } catch (Throwable t) {
            fail("libffi 常量", t);
            return;
        }

        try (MemoryStack stack = MemoryStack.stackPush()) {
            FFICIF cif = FFICIF.calloc(stack);
            PointerBuffer atypes = stack.mallocPointer(1);
            atypes.put(0, LibFFI.ffi_type_sint32.address());
            int rc = LibFFI.ffi_prep_cif(cif, LibFFI.FFI_DEFAULT_ABI, LibFFI.ffi_type_sint32, atypes);
            mark("ffi_prep_cif -> " + rc + "  (FFI_OK=" + LibFFI.FFI_OK + ")");
            mark("cif.abi=" + cif.abi() + " cif.nargs=" + cif.nargs());
        } catch (Throwable t) {
            fail("ffi_prep_cif", t);
        }

        // 自定义 FFIType 结构（验证 FFIType 的 size/alignment 访问器）
        try (MemoryStack stack = MemoryStack.stackPush()) {
            FFIType t = FFIType.calloc(stack);
            t.type(LibFFI.FFI_TYPE_STRUCT);
            mark("自定义 FFIType.type=" + t.type() + " size=" + t.size() + " align=" + t.alignment());
        } catch (Throwable t) {
            fail("FFIType", t);
        }
        mark("END");
    }
}
