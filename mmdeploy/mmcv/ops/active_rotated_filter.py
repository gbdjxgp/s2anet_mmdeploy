# Copyright (c) GBDJ. All rights reserved.
from mmdeploy.core import SYMBOLIC_REWRITER

#ActiveRotatedFilterFunction
@SYMBOLIC_REWRITER.register_symbolic(
    'mmcv.ops.active_rotated_filter.__self__', backend='default')
def active_rotated_filter_default(ctx,
                         g,
                         input,
                         indices):
    """Rewrite symbolic function for default backend."""
    return g.op(
        'mmdeploy::MMCVActiveRotatedFilter',
        input,
        indices)
