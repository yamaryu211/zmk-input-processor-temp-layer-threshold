/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_temp_layer_threshold

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Constants and Types */
#define MAX_LAYERS ZMK_KEYMAP_LAYERS_LEN

struct temp_layer_threshold_config {
    int16_t require_prior_idle_ms;
    int32_t reset_timeout_ms;
    const uint16_t *excluded_positions;
    size_t num_positions;
};

struct temp_layer_threshold_state {
    uint8_t toggle_layer;
    bool is_active;
    bool threshold_reached;
    int64_t last_tapped_timestamp;
    int64_t last_movement_timestamp;
    int32_t accumulated_movement;
    int32_t current_threshold;
};

struct temp_layer_threshold_data {
    const struct device *dev;
    struct k_mutex lock;
    struct temp_layer_threshold_state state;
};

/* Static Work Queue Items */
static struct k_work_delayable layer_disable_works[MAX_LAYERS];

/* Position Search */
static bool position_is_excluded(const struct temp_layer_threshold_config *config, uint32_t position) {
    if (!config->excluded_positions || !config->num_positions) {
        return false;
    }

    const uint16_t *end = config->excluded_positions + config->num_positions;
    for (const uint16_t *pos = config->excluded_positions; pos < end; pos++) {
        if (*pos == position) {
            return true;
        }
    }

    return false;
}

/* Timing Check */
static bool should_quick_tap(const struct temp_layer_threshold_config *config, int64_t last_tapped,
                             int64_t current_time) {
    return (last_tapped + config->require_prior_idle_ms) > current_time;
}

/* Layer State Management */
static void update_layer_state(struct temp_layer_threshold_state *state, bool activate) {
    if (state->is_active == activate) {
        return;
    }

    state->is_active = activate;
    if (activate) {
        zmk_keymap_layer_activate(state->toggle_layer, false);
        LOG_DBG("Layer %d activated (threshold)", state->toggle_layer);
    } else {
        zmk_keymap_layer_deactivate(state->toggle_layer, false);
        state->threshold_reached = false;
        state->accumulated_movement = 0;
        LOG_DBG("Layer %d deactivated (threshold)", state->toggle_layer);
    }
}

struct layer_state_action {
    uint8_t layer;
    bool activate;
};

K_MSGQ_DEFINE(temp_layer_threshold_action_msgq, sizeof(struct layer_state_action),
              CONFIG_ZMK_INPUT_PROCESSOR_TEMP_LAYER_THRESHOLD_MAX_ACTION_EVENTS, 4);

static void layer_action_work_cb(struct k_work *work) {
    const struct device *dev = DEVICE_DT_INST_GET(0);
    struct temp_layer_threshold_data *data = (struct temp_layer_threshold_data *)dev->data;

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        LOG_ERR("Error locking for updating %d", ret);
        return;
    }

    struct layer_state_action action;

    while (k_msgq_get(&temp_layer_threshold_action_msgq, &action, K_MSEC(10)) >= 0) {
        if (!action.activate) {
            if (zmk_keymap_layer_active(action.layer)) {
                update_layer_state(&data->state, false);
            }
        } else {
            update_layer_state(&data->state, true);
        }
    }

    k_mutex_unlock(&data->lock);
}

static K_WORK_DEFINE(layer_action_work, layer_action_work_cb);

/* Work Queue Callback */
static void layer_disable_callback(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    int layer_index = ARRAY_INDEX(layer_disable_works, d_work);

    struct layer_state_action action = {.layer = layer_index, .activate = false};

    k_msgq_put(&temp_layer_threshold_action_msgq, &action, K_MSEC(10));
    k_work_submit(&layer_action_work);
}

/* Event Handlers */
static int handle_layer_state_changed(const struct device *dev, const zmk_event_t *eh) {
    struct temp_layer_threshold_data *data = (struct temp_layer_threshold_data *)dev->data;
    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }
    if (!zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(data->state.toggle_layer))) {
        LOG_DBG("Deactivating layer that was activated by this processor");
        data->state.is_active = false;
        data->state.threshold_reached = false;
        data->state.accumulated_movement = 0;
        k_work_cancel_delayable(&layer_disable_works[data->state.toggle_layer]);
    }
    ret = k_mutex_unlock(&data->lock);
    if (ret < 0) {
        return ret;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_position_state_changed(const struct device *dev, const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct temp_layer_threshold_data *data = (struct temp_layer_threshold_data *)dev->data;
    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    const struct temp_layer_threshold_config *cfg = dev->config;

    if (data->state.is_active && cfg->excluded_positions && cfg->num_positions > 0) {
        if (!position_is_excluded(cfg, ev->position)) {
            LOG_DBG("Position not excluded, deactivating layer");
            update_layer_state(&data->state, false);
        }
    }

    k_mutex_unlock(&data->lock);

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_keycode_state_changed(const struct device *dev, const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct temp_layer_threshold_data *data = (struct temp_layer_threshold_data *)dev->data;

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    LOG_DBG("Setting last_tapped_timestamp to: %lld", ev->timestamp);
    data->state.last_tapped_timestamp = ev->timestamp;

    ret = k_mutex_unlock(&data->lock);
    if (ret < 0) {
        return ret;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_state_changed_dispatcher(const struct device *dev, const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) != NULL) {
        return handle_layer_state_changed(dev, eh);
    } else if (as_zmk_position_state_changed(eh) != NULL) {
        return handle_position_state_changed(dev, eh);
    } else if (as_zmk_keycode_state_changed(eh) != NULL) {
        return handle_keycode_state_changed(dev, eh);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define DISPATCH_EVENT(inst)                                                                       \
    {                                                                                              \
        int err = handle_state_changed_dispatcher(DEVICE_DT_INST_GET(inst), eh);                   \
        if (err < 0) {                                                                             \
            return err;                                                                            \
        }                                                                                          \
    }

static int handle_event_dispatcher(const zmk_event_t *eh) {
    DT_INST_FOREACH_STATUS_OKAY(DISPATCH_EVENT)

    return 0;
}

/* Driver Implementation */
static int temp_layer_threshold_handle_event(const struct device *dev, struct input_event *event,
                                             uint32_t param1, uint32_t param2, uint32_t param3,
                                             struct zmk_input_processor_state *state) {
    if (param1 >= MAX_LAYERS) {
        LOG_ERR("Invalid layer index: %d", param1);
        return -EINVAL;
    }

    /* Only process relative movement events */
    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct temp_layer_threshold_data *data = (struct temp_layer_threshold_data *)dev->data;

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    const struct temp_layer_threshold_config *cfg = dev->config;
    int64_t now = k_uptime_get();

    data->state.toggle_layer = param1;
    data->state.current_threshold = param3;

    /* Reset accumulated movement if timeout elapsed */
    if ((now - data->state.last_movement_timestamp) > cfg->reset_timeout_ms) {
        data->state.accumulated_movement = 0;
        data->state.threshold_reached = false;
        LOG_DBG("Movement timeout, reset accumulated");
    }
    data->state.last_movement_timestamp = now;

    /* Accumulate movement */
    data->state.accumulated_movement += abs(event->value);

    /* Check if we should activate */
    bool should_activate = false;

    if (data->state.is_active) {
        /* Already active, just reschedule timeout */
        should_activate = false;
    } else if (should_quick_tap(cfg, data->state.last_tapped_timestamp, now)) {
        /* Quick tap prevention */
        should_activate = false;
    } else if (param3 > 0 && !data->state.threshold_reached) {
        /* Threshold mode: check if threshold reached */
        if (data->state.accumulated_movement >= param3) {
            data->state.threshold_reached = true;
            should_activate = true;
            LOG_DBG("Threshold %d reached with accumulated %d", param3, data->state.accumulated_movement);
        }
    } else if (param3 == 0) {
        /* No threshold, activate immediately */
        should_activate = true;
    } else if (data->state.threshold_reached) {
        /* Threshold already reached previously */
        should_activate = true;
    }

    if (should_activate && !data->state.is_active) {
        struct layer_state_action action = {.layer = param1, .activate = true};

        ret = k_msgq_put(&temp_layer_threshold_action_msgq, &action, K_MSEC(10));
        if (ret < 0) {
            LOG_ERR("Failed to enqueue action to enable layer %d (%d)", param1, ret);
        } else {
            k_work_submit(&layer_action_work);
        }
    }

    /* Reschedule layer timeout */
    if (param2 > 0 && (data->state.is_active || should_activate)) {
        k_work_reschedule(&layer_disable_works[param1], K_MSEC(param2));
    }

    k_mutex_unlock(&data->lock);

    return ZMK_INPUT_PROC_CONTINUE;
}

static int temp_layer_threshold_init(const struct device *dev) {
    struct temp_layer_threshold_data *data = (struct temp_layer_threshold_data *)dev->data;
    k_mutex_init(&data->lock);

    for (int i = 0; i < MAX_LAYERS; i++) {
        k_work_init_delayable(&layer_disable_works[i], layer_disable_callback);
    }

    return 0;
}

/* Driver API */
static const struct zmk_input_processor_driver_api temp_layer_threshold_driver_api = {
    .handle_event = temp_layer_threshold_handle_event,
};

/* Event Listeners */
ZMK_LISTENER(processor_temp_layer_threshold, handle_event_dispatcher);
ZMK_SUBSCRIPTION(processor_temp_layer_threshold, zmk_layer_state_changed);

#define NEEDS_POSITION_HANDLERS(n, ...) DT_INST_PROP_HAS_IDX(n, excluded_positions, 0)
#define NEEDS_KEYCODE_HANDLERS(n, ...) (DT_INST_PROP_OR(n, require_prior_idle_ms, 0) > 0)

#if DT_INST_FOREACH_STATUS_OKAY_VARGS(NEEDS_POSITION_HANDLERS, ||)
ZMK_SUBSCRIPTION(processor_temp_layer_threshold, zmk_position_state_changed);
#endif

#if DT_INST_FOREACH_STATUS_OKAY_VARGS(NEEDS_KEYCODE_HANDLERS, ||)
ZMK_SUBSCRIPTION(processor_temp_layer_threshold, zmk_keycode_state_changed);
#endif

/* Device Instantiation */
#define TEMP_LAYER_THRESHOLD_INST(n)                                                               \
    static struct temp_layer_threshold_data processor_temp_layer_threshold_data_##n = {};          \
    static const uint16_t excluded_positions_##n[] = DT_INST_PROP(n, excluded_positions);          \
    static const struct temp_layer_threshold_config processor_temp_layer_threshold_config_##n = {  \
        .require_prior_idle_ms = DT_INST_PROP_OR(n, require_prior_idle_ms, 0),                     \
        .reset_timeout_ms = DT_INST_PROP_OR(n, reset_timeout_ms, 200),                             \
        .excluded_positions = excluded_positions_##n,                                              \
        .num_positions = DT_INST_PROP_LEN(n, excluded_positions),                                  \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, temp_layer_threshold_init, NULL,                                      \
                          &processor_temp_layer_threshold_data_##n,                                \
                          &processor_temp_layer_threshold_config_##n, POST_KERNEL,                 \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &temp_layer_threshold_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TEMP_LAYER_THRESHOLD_INST)
