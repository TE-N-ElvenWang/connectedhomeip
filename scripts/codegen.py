#!/usr/bin/env python
# Copyright (c) 2022 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import click
import logging
import coloredlogs
import enum

try:
    from idl.matter_idl_parser import CreateParser
except:
    import os
    import sys
    sys.path.append(os.path.abspath(os.path.dirname(__file__)))
    from idl.matter_idl_parser import CreateParser

from idl.generators import FileSystemGeneratorStorage
from idl.generators.java import JavaGenerator


class CodeGeneratorTypes(enum.Enum):
    JAVA = enum.auto()

    def CreateGenerator(self, *args, **kargs):
        if self == CodeGeneratorTypes.JAVA:
            return JavaGenerator(*args, **kargs)
        else:
            raise Error("Unknown code generator type")


# Supported log levels, mapping string values required for argument
# parsing into logging constants
__LOG_LEVELS__ = {
    'debug': logging.DEBUG,
    'info': logging.INFO,
    'warn': logging.WARN,
    'fatal': logging.FATAL,
}

__GENERATORS__ = {
    'java': CodeGeneratorTypes.JAVA
}


@click.command()
@click.option(
    '--log-level',
    default='INFO',
    type=click.Choice(__LOG_LEVELS__.keys(), case_sensitive=False),
    help='Determines the verbosity of script output')
@click.option(
    '--generator',
    default='JAVA',
    type=click.Choice(__GENERATORS__.keys(), case_sensitive=False),
    help='What code generator to run')
@click.option(
    '--output-dir',
    type=click.Path(exists=False),
    default=".",
    help='Where to generate the code')
@click.option(
    '--dry-run',
    default=False,
    is_flag=True,
    help='If to actually generate')
@click.argument(
    'idl_path',
    type=click.Path(exists=True))
def main(log_level, generator, output_dir, dry_run, idl_path):
    """
    Parses MATTER IDL files (.matter) and performs SDK code generation
    as set up by the program arguments.
    """
    coloredlogs.install(level=__LOG_LEVELS__[
                        log_level], fmt='%(asctime)s %(levelname)-7s %(message)s')
    logging.info("Parsing idl from %s" % idl_path)
    idl_tree = CreateParser().parse(open(idl_path, "rt").read())

    logging.info("Running code generator %s" % generator)
    generator = __GENERATORS__[generator].CreateGenerator(
        storage=FileSystemGeneratorStorage(output_dir), idl=idl_tree)
    generator.render(dry_run)
    logging.info("Done")


if __name__ == '__main__':
    main()
