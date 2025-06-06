---
orphan: true
---

# Tool information

This tool reads out the supported elements and generates the appropriate PICS
files for the device. The tool outputs the PICS for all endpoints and outputs
these in a single folder.

Note: The tool does relay on what the the device is able to express and for now
there are areas which the tool can not cover:

-   PICS in base.xml
-   Only mandatory events are marked as supported, since the global attribute
    with list of events is provisional

# Setup

This tool uses the python environment used by the python_testing efforts, which
can be built using the below command.

```
scripts/build_python.sh -m platform -i out/python_env
```

Once the python environment is build it can be activated using this command:

```
source out/python_env/bin/activate
```

The script uses the PICS XML templates for generate the PICS output. These files
can be downloaded here:
[https://groups.csa-iot.org/wg/matter-csg/document/26122](https://groups.csa-iot.org/wg/matter-csg/document/26122)

NOTE: The tool has been verified using V24 PICS (used for Matter 1.2
certification)

# How to run

First change the directory to the tool location.

```
cd src/tools/PICS-generator/
```

The tool does, as mentioned above, have external dependencies, these are
provided to the tool using these arguments:

-   --pics-template is the absolute path to the folder containing the PICS
    templates
-   --pics-output is the absolute path to the output folder to be used
-   --dm-xml (Optional) is the absolute path to the spec scrape to use, located
    in the data_model folder in the root of the connectedhomeip repo. An example
    path is "connectedhomeip/data_model/master".

If the device has not been commissioned this can be done by passing in the
commissioning information:

```
python3 PICSGenerator.py --pics-template <pathToPicsTemplateFolder> --pics-output <outputPath> --commissioning-method ble-thread --discriminator <DESCRIMINATOR> --passcode <PASSCODE> --thread-dataset-hex <DATASET_AS_HEX>
```

or in case the device is e.g. an example running on a Linux/macOS system, use
the on-network commissioning:

```
python3 PICSGenerator.py --pics-template <pathToPicsTemplateFolder> --pics-output <outputPath> --commissioning-method on-network --discriminator <DESCRIMINATOR> --passcode <PASSCODE>
```

In case the device uses a development PAA, the following parameter should be
added.

```
--paa-trust-store-path credentials/development/paa-root-certs
```

In case the device uses a production PAA, the following parameter should be
added.

```
--paa-trust-store-path credentials/production/paa-root-certs
```

If a device has already been commissioned, the tool can be executed like this:

```
python3 PICSGenerator.py --pics-template <pathToPicsTemplateFolder> --pics-output <outputPath>
```

The tool can be used to generate PICS for a specific spec versions, this can be
done by providing the following tag in the command, if no path is provided the
tool will request the specification version from the device in the
BasicInformation cluster and use that to select DM scrape to use for the PICS
generation.

```
python3 XMLPICSValidator.py --pics-template <pathToPicsTemplateFolder> --dm-xml <pathToDmScrapeFolder>
```

If the tag is not provided

# Updates for future releases

Given each new release adds PICS files, to ensure the tool is able to map the
cluster names to the PICS XML files, the XMLPICSValidator script can be used to
validate the mapping and will inform in case a cluster can not be mapped to a
PICS XML file.

The purpose of this script is mainly to make the update of this tool to future
versions of Matter easier and is not intended as a script for generating the
PICS.

To run the XMLPICSValidator, the following command can be used:

```
python3 XMLPICSValidator.py --pics-template <pathToPicsTemplateFolder> --dm-xml <pathToDmScrapeFolder>
```

NOTE: The --dm-xml is required for this script, since it does not run against a
specific device.
