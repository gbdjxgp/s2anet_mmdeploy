# Copyright (c) OpenMMLab. All rights reserved.
from mmdeploy.core import FUNCTION_REWRITER


@FUNCTION_REWRITER.register_rewriter(
    func_name='mmrotate.models.detectors.RotatedSingleStageDetector'
    '.simple_test')
def single_stage_rotated_detector__simple_test(ctx,
                                               self,
                                               img,
                                               img_metas,
                                               rescale=False):
    """Rewrite `simple_test` of RotatedSingleStageDetector for default backend.

    Rewrite this function to early return the results to avoid post processing.
    The process is not suitable for exporting to backends and better get
    implemented in SDK.

    Args:
        ctx (ContextCaller): The context with additional information.
        self: The instance of the class
            SingleStageTextDetector.
        img (Tensor): Input images of shape (N, C, H, W).
            Typically these should be mean centered and std scaled.

    Returns:
        outs (Tensor): A feature map output from bbox_head. The tensor shape
            (N, C, H, W).
    """
    x = self.extract_feat(img)
    outs = self.bbox_head(x)
    outs = self.bbox_head.get_bboxes(*outs, img_metas, rescale=rescale)

    return outs

@FUNCTION_REWRITER.register_rewriter(
    func_name='mmrotate.models.detectors.S2ANet'
    '.simple_test')
def simple_test(ctx,self, img, img_meta, rescale=False):
    """Test function without test time augmentation.

    Args:
        imgs (list[torch.Tensor]): List of multiple images
        img_metas (list[dict]): List of image information.
        rescale (bool, optional): Whether to rescale the results.
            Defaults to False.

    Returns:
        list[list[np.ndarray]]: BBox results of each image and classes. \
            The outer list corresponds to each image. The inner list \
            corresponds to each class.
    """
    x = self.extract_feat(img)
    outs = self.fam_head(x)
    rois = self.fam_head.refine_bboxes(*outs)
    # rois: list(indexed by images) of list(indexed by levels)
    align_feat = self.align_conv(x, rois)
    outs = self.odm_head(align_feat)

    bbox_inputs = outs + (img_meta, self.test_cfg, rescale)
    bbox_results = self.odm_head.get_bboxes(*bbox_inputs, rois=rois)
    return bbox_results