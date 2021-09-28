const bindings = require("bindings")("dcmtk-wrapper.node");

bindings.dcmcjpeg("./input.dcm", "./output.dcm");
