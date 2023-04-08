# .kpsf File Format Reference

## Header info

|Field|Type|
|-----|----|
|Total length in seconds|32-bit signed integer|
|Total # of fingerprints|32-bit signed integer|

This is followed by a list of fingerprint records.

## Fingerprint record

For each fingerprint:

|Field|Type|
|-----|----|
|Path|String (length-prefixed w/ 32-bit signed int)|
|Length in seconds|32-bit float|
|# of hash/offset pairs|32-bit signed integer|

This is followed by a list of hash/offset pairs.

## Hash/offset pairs

For each hash/offset pair:

|Field|Type|
|-----|----|
|SHA1 hash|String (not length-prefixed; always 20 bytes)|
|Offset|32-bit signed integer|



