
<style>
div#container {
  background-color: rgb(240, 240, 255);
  padding: 0px;
}

div#workout-list {
  display: inline-block;
  height: 400px;
  overflow: scroll;
  vertical-align: top;
}

.frf-split-data {
  display: block;
}

.frf-edit-input {
  display: inline-block;
}

.frf-label {
  margin-left: 3px;
  margin-right: 3px;
}

.frf-edit {
  border: 1px solid black;
  position: relative;
  width: 320px;
  background-color: rgb(192,128,128);
  display: inline-block;
}

.frf-time {
  display: inline-block;
}

.frf-time input {
  border: 1px solid black;
  background-color: rgb(192, 240, 192);
  width: 70px;
  height: 20px;
}

.frf-dist {
  display: inline-block;
}

.frf-dist input {
  border: 1px solid black;
  background-color: rgb(192, 240, 192);
  width: 50px;
  height: 20px;
}


.frf-edit-menu {
  background-color: white;
}

div#workout-edit {
  display: inline-block;
  width: 720px;
  height: 100%;
  border: 1px solid black;
  vertical-align: top;
}
</style>
<script src="https://cdn.jsdelivr.net/npm/axios/dist/axios.js">
</script>


<div id="app">
<h1>Fast Running Friend</h1>
<div id="container">
  <div id="workout-list">
    <div v-for="d in workouts">
      <frf-workout-link :d="d" :key="d" :root="get_root()"></frf-workout>
    </div>
  </div>
  <div id="workout-edit" v-if="cur_workout">
    <frf-workout-edit :workout="cur_workout"></frf-workout-edit>
  </div>
</div>
</div>

<script src="https://cdn.jsdelivr.net/npm/vue/dist/vue.js">
</script>
<script>

var DEFAULT_ICON_SIZE = 20;

Vue.component('frf-workout-edit', {
  props: ["workout", "root"],
  template: '<div><div><frf-edit-text :text="workout.comment"></frf-edit-text></div><div>' +
    '<template v-for="l in workout.legs"><frf-leg :l="l"></frf-leg></template></div></div>'
});

Vue.component('frf-edit-text', {
  props: {
    root: Object,
    text: String,
    maxlen: {default: 128, type: Number},
    editwidth: {default: "300px", type: String},
    editheight: {default: "200px", type: String},
    width: {default: "300px", type: String},
    height: {default: "60px", type: String},
  },
  data: function() {
    return {
      in_edit: false
    };
  },
  computed: {
    edit_style: function () {
      return {width: this.editwidth, height: this.editheight, margin: '5px'};
    },
    preview_style: function () {
      return {width: this.width, height: this.height, margin: '5px'};
    },
    preview_text: function() {
      return this.text.substr(0, this.maxlen) + (this.text.length > this.maxlen ? "..." : "");
    },
    edit_icon_style: function() {
      return {position: "absolute", top: "5px", left: "5px"};
    }
  },
  methods: {
    open_edit: function() {
      this.in_edit = true;
    },
    close_edit: function() {
      this.in_edit = false;
    }
  },
  template: '<div class="frf-edit">' +
    '<textarea v-bind:style="edit_style" v-on:blur="close_edit()"' +
    ' v-if="in_edit">{{text}}</textarea>' +
    '<div v-else v-on:click="open_edit()"' +
    ' v-bind:style="preview_style">{{preview_text}}</div>' +
    '</div>'
});

Vue.component('frf-edit-icon', {
  props: {
    width: { default: DEFAULT_ICON_SIZE, type: Number},
    height: { default: DEFAULT_ICON_SIZE, type: Number},
  },
  template: '<svg xmlns="http://www.w3.org/2000/svg" version="1.1"' +
   'viewBox="0 0 100 100" :width="width" :height="height">' +
'<path d="M77.926,94.924H8.217C6.441,94.924,5,93.484,5,91.706V21.997c0-1.777,1.441-3.217,3.217-3.217h34.854' + 'c1.777,0,3.217,1.441,3.217,3.217s-1.441,3.217-3.217,3.217H11.435v63.275h63.274V56.851c0-1.777,1.441-3.217,' + '3.217-3.217 c1.777,0,3.217,1.441,3.217,3.217v34.855C81.144,93.484,79.703,94.924,77.926,94.924z"/>' +
'<path d="M94.059,16.034L84.032,6.017c-1.255-1.255-3.292-1.255-4.547,0l-9.062,9.073L35.396,50.116' + 'c-0.29,0.29-0.525,0.633-0.686,1.008l-7.496,17.513c-0.526,1.212-0.247,2.617,0.676,3.539c0.622,0.622,1.437,0.944,2.274,0.944' + 'c0.429,0,0.858-0.086,1.276-0.257l17.513-7.496c0.375-0.161,0.719-0.397,1.008-0.686l35.026-35.026l9.073-9.062' + 'C95.314,19.326,95.314,17.289,94.059,16.034z M36.286,63.79l2.928-6.821l3.893,3.893L36.286,63.79z' + ' M46.925,58.621l-5.469-5.469 L73.007,21.6l5.47,5.469L46.925,58.621z' + 'M81.511,24.034l-5.469-5.469l5.716-5.716l5.469,5.459L81.511,24.034z"/></svg>'
});

Vue.component('frf-close-edit-icon', {
  props: {
    width: { default: DEFAULT_ICON_SIZE, type: Number},
    height: { default: DEFAULT_ICON_SIZE, type: Number},
  },

  template: '<svg xmlns="http://www.w3.org/2000/svg" :width="width" :height="height" fill="currentColor" class="bi bi-arrow-down-left-square" viewBox="0 0 16 16">' +
  '<path fill-rule="evenodd" d="M15 2a1 1 0 0 0-1-1H2a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V2zM0 2a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2V2zm10.096 3.146a.5.5 0 1 1 .707.708L6.707 9.95h2.768a.5.5 0 1 1 0 1H5.5a.5.5 0 0 1-.5-.5V6.475a.5.5 0 1 1 1 0v2.768l4.096-4.097z"/></svg>'
});


Vue.component('frf-workout-link', {
  props: ["d", "root"],
  template: "<div><button v-on:click='review_workout(d)'>{{d}}</button></div>",
  methods: {
    review_workout: function(d) {
      this.root.review_workout(d);
    }
  }
});

Vue.component('frf-time', {
  props: ["ms", "parent", "oncloseedit", "label"],
  data: function() {
    return {
      in_edit: false,
      is_valid: true,
      edit_ms: null,
    };
  },
  mounted: function() {
    this.edit_ms = this.ms;
  },
  methods: {
    validate: function(s) {
      this.is_valid = !isNaN(time_to_sec(s));
      return this.is_valid;
    },
    finish_edit: function(s) {
      if (!this.validate(s))
        return;

      this.edit_ms = time_to_sec(s) * 1000;
      if (this.oncloseedit)
        this.oncloseedit(this.edit_ms);
    }
  },
  computed: {
    t_str: function() {
      if (this.edit_ms === null)
        this.edit_ms = this.ms;
      return sec_to_time(this.edit_ms/1000);
    }
  },
  watch: {
    ms: function(val) {
      this.edit_ms = val;
    }
  },
  template: '<div class="frf-time"><span class="frf-label">{{label}}</span>' +
    '<frf-edit-input :parent="parent" :validate="validate" ' +
    ':oncloseedit="finish_edit" :val="t_str"></frf-edit-input></div>'
});

Vue.component('frf-edit-input', {
  props: ["val", "oncloseedit", "parent", "validate"],
  data: function() {
    return {
      val_str: "",
      in_edit: false,
      is_valid: true,
    }
  },
  methods: {
    close_edit: function() {
      this.in_edit = false;
      if (this.validate && !this.validate(this.val_str))
      {
        this.val_str = this.val;
        this.is_valid = true;
      }
      if (this.oncloseedit)
        this.oncloseedit(this.val_str);
    },
    open_edit: function() {
      this.in_edit = true;
    }
  },
  mounted: function() {
    this.val_str = this.val;
  },
  computed: {
    input_style: function() {
      if (!this.is_valid)
        return {"background-color" : "pink"};
      return {};
    },
    cur_val: {
      get: function() {
        return this.val_str;
      },
      set: function(v) {
        this.val_str = v;
        if (this.validate)
          this.is_valid = this.validate(v);
      }
    }
  },
  watch: {
    val: function(val) {
      this.cur_val = val;
    }
  },
  template: '<div class="frf-edit-input">' +
    '<input v-on:blur="close_edit()" v-if="in_edit"' +
    'v-bind:style="input_style" type="text" v-model="cur_val" />' +
    '<div class="frf-no-edit" v-on:click="open_edit()" v-else>{{cur_val}}</div></div>'
});

Vue.component('frf-dist', {
  props: ["d", "parent", "oncloseedit", "label"],
  data: function() {
    return {
      in_edit: false,
      is_valid: false,
      edit_d: this.d.toFixed(3),
    }
  },
  methods: {
    finish_edit: function(d_str) {
      if (!this.validate(d_str))
        return;
      this.edit_d = d_str;
      if (this.oncloseedit)
        this.oncloseedit(d_str);
    },
    validate: function(d_str) {
      return !isNaN(d_str);
    }
  },
  watch: {
    d: function(val) {
      this.edit_d = parseFloat(val).toFixed(3);
    }
  },
  template: '<div class="frf-dist"><span class="frf-label">{{label}}</span>' +
    '<frf-edit-input :val="edit_d" :validate="validate"' +
    ' :oncloseedit="finish_edit"' +
    ':parent="parent"></frf-edit-input>' +
    '<span class="frf-label">M</span></div>'
});

Vue.component('frf-split', {
  props: ["s"],
  data: function() {
    return {
      d: this.s.dist,
      t: this.s.time,
    };
  },
  methods: {
    update_t: function(val) {
      this.t = val;
    },
    update_d: function(val) {
      this.d = val;
    },
    update_pace: function(val) {
      if (val)
      {
        this.d = this.t / val;
        console.log("new d", this.d);
        this.$mount();

      }
    }
  },
  computed: {
    me: function() {
      return this;
    },
    pace: {
      get: function() {
        if (!this.d)
          return 0;
        return this.t/this.d;
      },
      set: function(val) {
        this.update_pace(val);
      }
    }
  },
  template: '<div class="frf-split>">' +
    '<div class="frf-split-data">' +
    '<frf-dist label="Dist" :parent="me" :d="d" :oncloseedit="update_d"></frf-dist>' +
    '<frf-time label="Time" :parent="me" :ms="t" :oncloseedit="update_t"></frf-time>' +
    '<frf-time label="Pace" :parent="me" :ms="pace" :oncloseedit="update_pace"></frf-time>' +
    '</div>' +
    '<frf-edit-text :text="s.comment"></frf-edit-text>' +
    '</div>'
});

Vue.component('frf-leg', {
  props: ["l"],
  computed: {
    split_for_leg: function() {
      var sp = { dist: 0, time: 0, comment: this.l.comment};

      for (var i = 0; i < this.l.splits.length; i++)
      {
        var cur_sp = this.l.splits[i];
        sp.dist += cur_sp.dist;
        sp.time += cur_sp.time;
      }

      return sp;
    }
  },
  template: '<div class="frf-leg">' +
    '<frf-split :s="split_for_leg"></frf-split>' +
    '<div>' +
    '</div><div v-if="l.splits.length>1"><template v-for="s in l.splits" >' +
    '<frf-split :s="s"></frf-split></template></div></div>'
});

new Vue({
  el: '#app',
  data: function() {
    return {
      workouts: [],
      cur_workout: null
    }
  },
  methods: {
    get_root: function() {
      return this;
    },
    review_workout: function (d) {
      var vm = this;
      axios.get("/api/workout/" + d).then(function (rsp) {
        vm.cur_workout = rsp.data;
      });
    }
  },
  mounted: function() {
    var vm = this;
    axios.get("/api/review").then(function (rsp) {
      vm.workouts = rsp.data;
    });
  }
});
</script>
