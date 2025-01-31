#!/bin/bash

##########################################################
##                                                      ##
## Shell script for generating a boards manager release ##
## Created by MCUdude                                   ##
## Requires wget, jq and a bash environment             ##
##                                                      ##
##########################################################

# Change these to match your repo
AUTHOR=felias-fogg     # Github username
REALAUTHOR=drazzy.com  # real author
REPOSITORY=ATTinyCore # Github repo name
SUFFIX=_plus_Debug

# Get the download URL for the latest release from Github
DOWNLOAD_URL=$(curl -s https://api.github.com/repos/$AUTHOR/$REPOSITORY/releases/latest | grep "tarball_url" | awk -F\" '{print $4}')

# Download file
wget --no-verbose $DOWNLOAD_URL

# Get filename
DOWNLOADED_FILE=$(echo $DOWNLOAD_URL | awk -F/ '{print $8}')

# Add .tar.bz2 extension to downloaded file
mv $DOWNLOADED_FILE ${DOWNLOADED_FILE}.tar.bz2

# Extract downloaded file and place it in a folder
printf "\nExtracting folder ${DOWNLOADED_FILE}.tar.bz2 to $REPOSITORY-${DOWNLOADED_FILE#"v"}\n"
mkdir -p "$REPOSITORY-${DOWNLOADED_FILE#"v"}" && tar -xzf ${DOWNLOADED_FILE}.tar.bz2 -C "$REPOSITORY-${DOWNLOADED_FILE#"v"}" --strip-components=1
printf "Done!\n"

# Move files out of the avr folder
mv $REPOSITORY-${DOWNLOADED_FILE#"v"}/avr/* $REPOSITORY-${DOWNLOADED_FILE#"v"}

# Delete downloaded file and empty avr folder
rm -rf ${DOWNLOADED_FILE}.tar.bz2 $REPOSITORY-${DOWNLOADED_FILE#"v"}/avr

# Compress folder to tar.bz2
printf "\nCompressing folder $REPOSITORY-${DOWNLOADED_FILE#"v"} to $REPOSITORY-${DOWNLOADED_FILE#"v"}.tar.bz2\n"
tar -cjSf $REPOSITORY-${DOWNLOADED_FILE#"v"}.tar.bz2 $REPOSITORY-${DOWNLOADED_FILE#"v"}
printf "Done!\n"

# Get file size on bytes
FILE_SIZE=$(wc -c "$REPOSITORY-${DOWNLOADED_FILE#"v"}.tar.bz2" | awk '{print $1}')

# Get SHA256 hash
SHA256="SHA-256:$(shasum -a 256 "$REPOSITORY-${DOWNLOADED_FILE#"v"}.tar.bz2" | awk '{print $1}')"

# Create Github download URL
URL="https://${AUTHOR}.github.io/${REPOSITORY}/$REPOSITORY-${DOWNLOADED_FILE#"v"}.tar.bz2"

cp "package_${REALAUTHOR}_${REPOSITORY}${SUFFIX}_index.json" "package_${REALAUTHOR}_${REPOSITORY}${SUFFIX}_index.json.tmp"

# Add new boards release entry
jq -r                                   \
--arg repository $REPOSITORY            \
--arg version    ${DOWNLOADED_FILE#"v"} \
--arg url        $URL                   \
--arg checksum   $SHA256                \
--arg file_size  $FILE_SIZE             \
--arg file_name  $REPOSITORY-${DOWNLOADED_FILE#"v"}.tar.bz2  \
'.packages[].platforms[.packages[].platforms | length] |= . +
{
  "name": $repository,
  "architecture": "avr",
  "version": $version,
  "category": "Contributed",
  "url": $url,
  "archiveFileName": $file_name,
  "checksum": $checksum,
  "size": $file_size,
  "boards": [
            { "name": "<b>Program via ISP or Serial:</b> ATtiny841/441, ATtiny85/45/25, ATtiny84/44/24, ATtiny1634, ATtiny861/461/261, ATtiny167/87, ATtiny43, ATtiny828, ATtiny88/48, ATtiny4313/2313"
            },
            {
              "name": "<br/><b>USB (Micronucleus) Support:</b> DigiSpark (t85), Digispark Pro (t167), MH-ET (t88), Wattuino/Nanite/etc (t841), CaliforniaSTEAM (t84)"
            },
            {
              "name": "<br/><b>Windows users:</b> If USB drivers are not already installed, run the post_install.bat manually or DL from <a href=https://azduino.com/bin/micronucleus>https://azduino.com/bin/micronucleus</a>"
            }
  
  ],
  "toolsDependencies": [
    {
      "packager": "arduino",
      "name": "avr-gcc",
      "version": "7.3.0-atmel3.6.1-arduino7"
    },
    {
      "packager": "arduino",
      "name": "avrdude",
      "version": "6.3.0-arduino18"
    },
    {
      "packager": "ATTinyCore",
      "name": "micronucleus",
      "version": "2.5-azd1b"
    },
    {
      "packager": "ATTinyCore",
      "name": "dw-link-tools",
      "version": "1.3.0"
    }   
  ]
}' "package_${REALAUTHOR}_${REPOSITORY}${SUFFIX}_index.json.tmp" > "package_${REALAUTHOR}_${REPOSITORY}${SUFFIX}_index.json"

# Remove files that's no longer needed
rm -rf "$REPOSITORY-${DOWNLOADED_FILE#"v"}" "package_${REALAUTHOR}_${REPOSITORY}${SUFFIX}_index.json.tmp"
