"""Velocita model: passive market-data research and shadow quoting.

This package is the market-data/feature edge and the offline calibration service
described in ADR 0001. It produces exactly two things the Raft Decision/OMS
cluster understands:

  * `DecisionFrame` - bounded canonical evidence proposed for one authoritative
    evaluation; and
  * `PolicyArtifact` - an immutable, versioned rule set.

Both are advisory. The cluster performs the final authoritative evaluation, and
only the C++ execution gateway can produce an external effect. This package has
no vocabulary for one.
"""

__version__ = "0.1.0"
__all__ = ["__version__"]
