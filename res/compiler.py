import re
import sys
import struct
import itertools


class Opcode:
    def __init__(self, code, args):
        self.code = code
        self.args = args


class Assembler:

    class Error(RuntimeError):
        pass

    Opcodes = {

        "mov":             Opcode("000", "RR"),
        "loadConst":       Opcode("001", "CR"),

        "add":             Opcode("010001", "RRR"),
        "sub":             Opcode("010010", "RRR"),
        "div":             Opcode("010011", "RRR"),
        "mod":             Opcode("010100", "RRR"),
        "mul":             Opcode("010101", "RRR"),

        "compare":         Opcode("01100", "RRR"),
        "jump":            Opcode("01101", "L"),
        "jumpEqual":       Opcode("01110", "LRR"),

        "read":            Opcode("10000", "RRRR"),
        "write":           Opcode("10001", "RRR"),
        "consoleRead":     Opcode("10010", "R"),
        "consoleWrite":    Opcode("10011", "R"),

        "createThread":    Opcode("10100", "LR"),
        "joinThread":      Opcode("10101", "R"),
        "hlt":             Opcode("10110", ""),
        "sleep":           Opcode("10111", "R"),

        "call":            Opcode("1100", "L"),
        "ret":             Opcode("1101", ""),

        "lock":            Opcode("1110", "R"),
        "unlock":          Opcode("1111", "R"),
    }

    DataAccessTypes = {
        "byte":  "00",
        "word":  "01",
        "dword": "10",
        "qword": "11",
    }

    def __init__(self, parser):
        self.__parser = parser
        self.__bytecode = []
        self.__offsets_list = []
        self.__offset_patches = {}

    def __append_bits(self, bits):
        self.__bytecode.extend(list(bits))

    def __assemble_register(self, argument_data):
        register_id, reference_type = argument_data

        if reference_type:
            self.__append_bits(
                "1" +
                self.DataAccessTypes[reference_type][::-1] +
                "{0:04b}".format(register_id)[::-1]
            )
        else:
            self.__append_bits(
                "0" + "{0:04b}".format(register_id)[::-1]
            )

    def __assemble_constant(self, argument_data):
        bits = "{0:064b}".format(int(argument_data, 0))[::-1]
        self.__append_bits(bits)

    def __assemble_label(self, argument_data):
        offset = self.__parser.code_labels.get(argument_data)
        if offset is None:
            raise Assembler.Error("Undefined code label %s" % argument_data)

        if offset < len(self.__offsets_list):
            address = self.__offsets_list[offset]
        else:
            address = 0
            self.__offset_patches[len(self.__bytecode)] = offset

        bits = "{0:032b}".format(address)[::-1]
        self.__append_bits(bits)

    def __apply_patches(self):
        for position, offset in self.__offset_patches.items():
            patched = "{0:032b}".format(self.__offsets_list[offset])[::-1]
            for i, bit in enumerate(patched):
                self.__bytecode[position + i] = bit

    def build(self, filepath):
        self.__bytecode = []
        self.__offsets_list = []
        self.__offset_patches = {}

        for instruction in self.__parser.code_section:

            self.__offsets_list.append(len(self.__bytecode))

            opcode = Assembler.Opcodes[instruction[0]]
            self.__append_bits(opcode.code)

            for argument_type, argument_data in zip(opcode.args, instruction[1:]):

                if argument_type == "R":
                    self.__assemble_register(argument_data)

                elif argument_type == "C":
                    self.__assemble_constant(argument_data)

                elif argument_type == "L":
                    self.__assemble_label(argument_data)

        self.__apply_patches()

        actual_data_size = len(self.__parser.data_section)

        if actual_data_size > self.__parser.data_size:
            print("Warning: bad .dataSize, was %d but used %d, expanding" %
                  (self.__parser.data_size, actual_data_size))
            self.__parser.data_size = actual_data_size

        if len(self.__bytecode) % 8 != 0:
            self.__bytecode += ["0"] * (8 - len(self.__bytecode) % 8)

        with open(filepath, "wb") as handle:
            handle.write(b"ESET-VM2")

            handle.write(struct.pack("I", len(self.__bytecode) // 8))
            handle.write(struct.pack("I", self.__parser.data_size))
            handle.write(struct.pack("I", len(self.__parser.data_section)))

            for i in range(0, len(self.__bytecode), 8):
                byte = int("".join(self.__bytecode[i:i + 8]), 2)
                handle.write(struct.pack("B", byte))

            for data_byte in self.__parser.data_section:
                handle.write(struct.pack("B", data_byte))


class Lexer:

    def __init__(self, filepath):
        self.__filepath = filepath

    def analyse(self):
        with open(self.__filepath, "r") as handle:
            for line_no, line in zip(itertools.count(), handle):
                tokens = line.split("#", 2)[0].split()

                if len(tokens) == 0:
                    continue

                yield line_no + 1, line, tokens


class Parser:

    class Error(RuntimeError):
        pass

    class ParseMode:
        Data = 1
        Code = 2

    def __init__(self, lexer):
        self.__lexer = lexer
        self.__parse_mode = None
        self.data_size = None
        self.data_labels = {}
        self.data_section = []
        self.code_labels = {}
        self.code_section = []
        self.last_parsed_line = ""
        self.last_parsed_line_no = -1

    def __parse_section(self, tokens):

        if tokens[0] == '.dataSize':
            if self.data_size is not None:
                raise Parser.Error("Double data size spotted")
            self.data_size = int(tokens[1])

        elif tokens[0] == '.code':
            self.__parse_mode = self.ParseMode.Code

        elif tokens[0] == '.data':
            self.__parse_mode = self.ParseMode.Data

        else:
            raise Parser.Error("Bad token")

    def __parse_label(self, tokens):
        label = tokens[0][:-1]

        if self.__parse_mode == self.ParseMode.Code:
            if label in self.code_labels:
                raise Parser.Error("Duplicated label")
            self.code_labels[label] = len(self.code_section)

        elif self.__parse_mode == self.ParseMode.Data:
            if label in self.data_labels:
                raise Parser.Error("Duplicated label")
            self.data_labels[label] = len(self.data_section)

        else:
            raise Parser.Error("Bad label")

    @staticmethod
    def __translate_argument_tokens(tokens):

        if len(tokens) == 0:
            return []
        return re.split(r"\s*,\s*", " ".join(tokens))

    __register_val_pattern = r"r(?P<id>[0-9]+)"

    __register_ref_pattern = r"(?P<ref>%s)\s*\[\s*%s\s*\]" % (
        "|".join(map(re.escape, Assembler.DataAccessTypes.keys())), __register_val_pattern)

    def __parse_code(self, tokens):
        opcode_name = tokens[0]

        opcode = Assembler.Opcodes.get(opcode_name)
        if not opcode:
            raise Parser.Error("Bad opcode [%s]" % opcode_name)

        argument_data = self.__translate_argument_tokens(tokens[1:])

        if len(argument_data) != len(opcode.args):
            raise Parser.Error("Bad opcode argument count")

        instruction = [opcode_name]

        for argument_type, argument_data in zip(opcode.args, argument_data):

            if argument_type == "R":
                match = None
                for pattern in (self.__register_val_pattern, self.__register_ref_pattern):
                    match = re.match(pattern, argument_data)
                    if match:
                        break

                if not match:
                    raise Parser.Error("Bad register argument type [%s]" % argument_data)

                groups = match.groupdict()
                register_id = int(groups["id"])

                if register_id > 16:
                    raise Parser.Error("Bad register argument type (too big)")

                instruction.append([register_id, groups.get("ref")])

            elif argument_type == "C":
                instruction.append(argument_data)

            elif argument_type == "L":
                instruction.append(argument_data)

            else:
                raise NotImplementedError("Bad argument type")

        self.code_section.append(instruction)

    def __parse_data(self, tokens):
        for entry in tokens:
            value = int(entry, 16)
            if value > 255:
                raise Parser.Error("Bad value in line")
            self.data_section.append(value)

    def analyse(self):

        self.__parse_mode = None

        self.data_size = None
        self.data_labels = {}
        self.data_section = []

        self.code_labels = {}
        self.code_section = []

        for self.last_parsed_line_no, self.last_parsed_line, tokens in self.__lexer.analyse():

            if tokens[0].startswith('.'):
                self.__parse_section(tokens)

            elif len(tokens) == 1 and tokens[0].endswith(':'):
                self.__parse_label(tokens)

            elif self.__parse_mode == self.ParseMode.Code:
                self.__parse_code(tokens)

            elif self.__parse_mode == self.ParseMode.Data:
                self.__parse_data(tokens)

            else:
                raise Parser.Error("Bad token")


def main(input_filepath, output_filepath):

    parser = Parser(Lexer(input_filepath))
    try:
        parser.analyse()

    except Parser.Error as exc:
        print("Parser error on line %d: %s" % (parser.last_parsed_line_no, str(exc)))
        sys.exit(2)

    assembler = Assembler(parser)
    try:
        assembler.build(output_filepath)

    except Assembler.Error as exc:
        print("Assembler error: %s" % str(exc))
        sys.exit(3)

    print("All ok")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: %s <input> <output>" % sys.argv[0])
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
