
<style>
div#container {
  background-color: rgb(240, 240, 255);
}

div#workout-list {
  display: inline-block;
}
div#workout-edit {
  display: inline-block;
}
</style>
<script src="https://cdn.jsdelivr.net/npm/axios/dist/axios.js">
</script>


<div id="app">
<h1>Fast Running Friend</h1>
<div id="container">
  <div id="workout-list">
    <div v-for="d in workouts">
      <frf-workout :d="d" :key="d" :root="get_root()"></frf-workout>
    </div>
  </div>
  <div id="workout-edit" v-if="cur_workout">
    {{cur_workout.comment}}
  </div>
</div>
</div>

<script src="https://cdn.jsdelivr.net/npm/vue/dist/vue.js">
</script>
<script>

Vue.component('frf-workout', {
  props: ["d", "root"],
  template: "<div><button v-on:click='review_workout(d)'>{{d}}</button></div>",
  methods: {
    review_workout: function(d) {
      this.root.review_workout(d);
    }
  }
});

Vue.component('frf-time', {
  props: ["t"],
  template: "<input size='8' type='text' value='sec_to_time(t)'>"
});

Vue.component('frf-split', {
  props: ["s"],
  template: "<tr><td>Split {{s.split_num}}</td><td><frf-time t='s.t'></frf-time></td></tr>"
});

Vue.component('frf-leg', {
  props: ["l"],
  template: "<table><template v-for='s in l.splits'><frf-split></frf-></template></table>"
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
