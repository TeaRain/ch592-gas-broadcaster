from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
TCC = os.environ.get(
    "TCC", "/tmp/ch592-toolchain-20261122/root/usr/bin/tcc"
)
TCC_RUNTIME = "/tmp/ch592-toolchain-20261122/root/usr/lib/x86_64-linux-gnu/tcc"

SOURCE = r'''
#include <stdint.h>
#include <stdio.h>
#include "gas_log_policy.h"

static uint32_t sequences[2][GAS_LOG_RECORDS_PER_BLOCK];
static uint8_t valid[2][GAS_LOG_RECORDS_PER_BLOCK];

static int read_sequence(uint8_t block, uint16_t index, uint32_t *sequence, void *context)
{
    (void)context;
    if (!valid[block][index]) return 0;
    *sequence = sequences[block][index];
    return 1;
}

int main(void)
{
    uint32_t recovered;
    sequences[1][250] = 42;
    valid[1][250] = 1;
    recovered = GasLogRecoverMaxSequence(41, read_sequence, 0);
    if (recovered != 42 || recovered + 1 != 43) {
        printf("valid unacknowledged write was not recovered: %u\n", recovered);
        return 1;
    }

    sequences[0][3] = 90;
    valid[0][3] = 0;
    if (GasLogRecoverMaxSequence(50, read_sequence, 0) != 50) {
        puts("invalid record changed the sequence");
        return 1;
    }

    sequences[0][3] = 100;
    valid[0][3] = 1;
    if (GasLogRecoverMaxSequence(50, read_sequence, 0) != 100) {
        puts("maximum sequence across both blocks was not selected");
        return 1;
    }

    puts("Gas log recovery policy checks passed");
    return 0;
}
'''


class GasLogPolicyTests(unittest.TestCase):
    def test_unacknowledged_valid_record_advances_next_sequence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "test.c"
            binary = root / "test"
            source.write_text(SOURCE, encoding="ascii")
            result = subprocess.run(
                [
                    TCC,
                    "-B" + TCC_RUNTIME,
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-I" + str(REPO / "APP/include"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(binary, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
