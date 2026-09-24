"""Regression checks for boot closure and ROM-backed overlay calls."""
from pathlib import Path
import unittest

from elftools.elf.elffile import ELFFile

from prepare_boot import closure


ROOT = Path(__file__).resolve().parents[2]
ELF = ROOT / "build/jfg.us.elf"
ROM = ROOT / "baseroms/baserom.us.z64"


class ClosureTest(unittest.TestCase):
    def test_existing_boot_roots_without_rom_argument(self):
        names = closure(ELF)
        self.assertEqual(len(names), 30)
        self.assertIn("runlinkDownloadCode", names)
        self.assertIn("ProcessRelocationEntry", names)

    def test_overlay_36_external_and_local_calls(self):
        # Keep the test limited to this overlay; other game modules belong to
        # separate profiles and are explicitly deferred here.
        with ELF.open("rb") as file:
            elf = ELFFile(file)
            symtab = elf.get_section_by_name(".symtab")
            deferred = tuple(symbol.name for symbol in symtab.iter_symbols()
                             if symbol["st_info"]["type"] == "STT_FUNC"
                             and isinstance(symbol["st_shndx"], int)
                             and elf.get_section(symbol["st_shndx"]).name != ".overlay_36")
        names = closure(ELF, roots=("mainInitRlo",), deferred=deferred, rom_path=ROM)
        self.assertIn("mainPreNMI", names)  # External JAL 0, resolved via ORT.
        self.assertIn("amInit", names)  # External call to another overlay.
        self.assertIn("objInitObjects", names)
        self.assertIn("func_overlay_36_02400250_1F55768", names)  # Local type-2 JAL.
        self.assertIn("func_overlay_36_02400564_1F55A7C", names)
        self.assertNotIn("MAX_vtx", names)  # Type-2 symbol_index is irrelevant.

    def test_overlay_requires_rom_for_relocations(self):
        with self.assertRaisesRegex(ValueError, "rom_path is required"):
            closure(ELF, roots=("mainInitRlo",))


if __name__ == "__main__":
    unittest.main()
