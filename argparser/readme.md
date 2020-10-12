# Command line argument parser (ArgParser)

`ArgParser` is a basic tool for parsing command line arguments. 
It support various types well. The built-in types are the following:

- std::string
- bool
- std::string
- int
- float
- double
- uint8_t
- uint16_t
- uint32_t
- uint64_t
- std:vector<T>

## Usage

The parser reads the input arguments of the main function and parses it according to the preset options. Option represents a command line argument (e.g. --verbose, --help, --key=value) and it alse supports the short name versioning (e.g. --verbose/-v, --help/-h). In addition, it also contains a description property which will be shown in the help menu.

Code example:

```
argparser::Argparser parser(argv.size(), &argv[0]);
parser.add_option(argparser::CreateArg("--verbose", "", "More details logging", false));
auto options = parser.parse();

```

The result of `parse()` is a map with keys and arguments. In order to get the result of the parsed it is needed to be casted to its valid type e.g.

```
bool verbose = std::dynamic_pointer_cast<Arg<bool>>(options["verbose"])->value());
```